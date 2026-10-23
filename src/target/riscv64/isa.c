#define _POSIX_C_SOURCE 200809L
#include "target/riscv64/isa.h"
#include "frontend/ast.h"
#include "core/core_ir.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static bool reject(char *why, size_t cap, const char *message) {
    if (why != NULL && cap > 0U) (void)snprintf(why, cap, "%s", message);
    return false;
}

bool minic_riscv_isa_has(const MinicRiscvIsa *isa, uint32_t requirements) {
    return isa != NULL && (isa->features & requirements) == requirements;
}

/* Small, exact, independently verifiable profiles. Do not parse a large
 * -march grammar and silently accept unsupported extensions. Other ISAs
 * remain completely unaffected, as does the original implicit RV64 target. */
bool minic_riscv_isa_resolve(const char *march, const char *mabi,
                             MinicRiscvIsa *isa, char *reason, size_t capacity) {
    MinicRiscvIsa resolved;
    if (isa == NULL || isa->xlen != 64U)
        return reject(reason, capacity, "only the RV64 backend is qualified");
    if (march == NULL) {
        if (mabi == NULL || strcmp(mabi, "lp64d") == 0) return true;
        return reject(reason, capacity,
                      "explicit -march is required for non-default RISC-V ABI");
    }
    resolved = *isa;
    resolved.features = 0U;
    resolved.strict_isa = true;
    if (strcmp(march, "rv64i") == 0) {
        /* I: integer base, no multiply/divide, floating point or atomics. */
        resolved.abi = MINIC_RISCV_ABI_LP64;
    } else if (strcmp(march, "rv64im") == 0) {
        resolved.features = MINIC_RISCV_FEATURE_M;
        resolved.abi = MINIC_RISCV_ABI_LP64;
    } else if (strcmp(march, "rv64imafd") == 0) {
        resolved.features = MINIC_RISCV_FEATURE_M | MINIC_RISCV_FEATURE_A |
                            MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D;
        resolved.abi = MINIC_RISCV_ABI_LP64D;
    } else {
        return reject(reason, capacity,
                      "supported explicit -march: rv64i, rv64im, rv64imafd; "
                      "other ISA combinations are not yet qualified");
    }
    if (mabi != NULL &&
        ((resolved.abi == MINIC_RISCV_ABI_LP64 && strcmp(mabi, "lp64") != 0) ||
         (resolved.abi == MINIC_RISCV_ABI_LP64D && strcmp(mabi, "lp64d") != 0))) {
        return reject(reason, capacity,
                      "ISA and ABI mismatch: rv64i/rv64im require lp64, "
                      "rv64imafd requires lp64d");
    }
    *isa = resolved;
    return true;
}

/* Planner decisions apply to reachable Core ops only. These are *machine*
 * choices, not frontend C-language admission: multiply may later choose a
 * qualified software lowering. The final emitted-ISA audit still catches all
 * implicit M/F/D instructions in helpers, calls and prologue/epilogue. */
/* A few historical RV64 scalar emitters only load the low 64 bits.
 * Never silently apply those emitters to a __int128 Core value under an
 * explicit ISA. Independent, already-pair-aware conversions, memory
 * operations, and right shifts retain their existing lowering. */
static bool core_operand_is_i128(const MinicCoreFunction *function,
                                 MinicCoreValueId value) {
    return function != NULL && value < function->value_count &&
           minic_type_is_int128_integer(function->values[value].type);
}

bool minic_riscv_isa_admit_core_instruction(
    const MinicRiscvIsa *isa, const MinicCoreFunction *function,
    const MinicCoreInstruction *instruction,
    char *reason, size_t capacity) {
    uint32_t required = 0U;
    const char *machine = NULL;
    const char *extension = NULL;
    if (isa == NULL || !isa->strict_isa || instruction == NULL) return true;
    /* Reject unqualified two-limb operations BEFORE even the first emitted
     * instruction. A 32-bit base ISA has no obligation to implement a C
     * compiler extension such as __int128, but it must not miscompile one. */
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        if (minic_type_is_int128_integer(instruction->type))
            return reject(reason, capacity,
                          "__int128 operation has no qualified two-limb lowering");
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (core_operand_is_i128(function, instruction->value.binary.left) ||
            core_operand_is_i128(function, instruction->value.binary.right))
            return reject(reason, capacity,
                          "__int128 comparison has no qualified two-limb lowering");
        break;
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        if (minic_type_is_int128_integer(instruction->type) ||
            core_operand_is_i128(function, instruction->value.operand))
            return reject(reason, capacity,
                          "__int128 scalar bitcast has no qualified pair lowering");
        break;
    default: break;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        /* Native MUL or qualified RV64I __muldi3 libcall.
         * 128-bit multiplication still emits MULHU and requires M. */
        if (!minic_type_is_int128_integer(instruction->type))
            return true;
        required = MINIC_RISCV_FEATURE_M;
        machine = "mulhu";
        extension = "M";
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
        /* 128-bit division/remainder already failed above. Ordinary
         * 32/64-bit operands use native M or external libgcc helpers. */
        return true;
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        /* RV64I pointer scaling selects SLLI for power-of-two sizes or
         * __muldi3 for odd record sizes. */
        return true;
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
        /* All admitted <=64-bit checked multiply shapes use libgcc in
         * strict-I; checked ADD and SUB need only I instructions. */
        return true;
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
        /* In LP64 with no F extension, the target-owned lowering uses
         * libgcc's __{add,sub,mul,div}sf3 soft-float interface. */
        if (!minic_riscv_isa_has(isa, MINIC_RISCV_FEATURE_F) &&
            isa->abi == MINIC_RISCV_ABI_LP64)
            return true;
        required = MINIC_RISCV_FEATURE_F;
        machine = "fadd.s";
        extension = "F";
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
        /* Exactly the corresponding __*df3 library call, not F/D code. */
        if (!minic_riscv_isa_has(
                isa, MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D) &&
            isa->abi == MINIC_RISCV_ABI_LP64)
            return true;
        required = MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D;
        machine = "fadd.d";
        extension = "F/D";
        break;
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
        required = MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D;
        machine = "fadd.d";
        extension = "F/D";
        break;
    default: break;
    }
    if (required == 0U || minic_riscv_isa_has(isa, required)) return true;
    if (reason != NULL && capacity > 0U)
        (void)snprintf(reason, capacity,
                       "RISC-V instruction '%s' requires %s; selected ISA lacks "
                       "hardware lowering and software lowering is not qualified",
                       machine, extension);
    return false;
}

static bool abi_type_contains_float(const MinicC0Program *program,
                                    MinicType type, unsigned depth) {
    size_t i;
    if (type.pointer_depth != 0U) return false;
    if (depth > 32U) return true;
    switch (type.base_kind) {
    case MINIC_TYPE_BASE_FLOAT:
    case MINIC_TYPE_BASE_DOUBLE:
    case MINIC_TYPE_BASE_LONG_DOUBLE:
    case MINIC_TYPE_BASE_FLOAT128:
    case MINIC_TYPE_BASE_FLOAT32:
    case MINIC_TYPE_BASE_FLOAT64:
    case MINIC_TYPE_BASE_FLOAT32X:
    case MINIC_TYPE_BASE_FLOAT64X:
        return true;
    case MINIC_TYPE_BASE_RECORD: {
        const MinicRecord *record = minic_c0_program_record(program, type.record_id);
        if (record == NULL || !record->is_complete) return true;
        for (i = 0U; i < record->field_count; ++i)
            if (abi_type_contains_float(program, record->fields[i].type, depth + 1U))
                return true;
        return false;
    }
    case MINIC_TYPE_BASE_ARRAY: {
        const MinicArrayType *array = minic_c0_program_array_type(program, type.array_type_id);
        return array == NULL || abi_type_contains_float(program, array->element_type, depth + 1U);
    }
    default: return false;
    }
}

bool minic_riscv_isa_soft_float_signature_supported(
    const MinicRiscvIsa *isa, const MinicC0Program *program,
    const MinicFunction *function) {
    size_t i;
    if (isa == NULL || !isa->strict_isa || isa->abi != MINIC_RISCV_ABI_LP64)
        return true;
    if (program == NULL || function == NULL ||
        abi_type_contains_float(program, function->return_type, 0U))
        return false;
    for (i = 0U; i < function->parameter_count; ++i)
        if (abi_type_contains_float(program, function->parameter_types[i], 0U))
            return false;
    return true;
}

/* Initial explicit LP64 ABI subset: one integer argument/return slot for
 * int/long/enum/pointer, or void return. Do NOT confuse "LP64 integer
 * calling convention" with "only C integer types": the full psABI also
 * defines floating values in integer registers, but MiniC's existing ABI
 * emitter has not qualified those shapes yet. */
static bool lp64_single_integer_slot(MinicType type, bool allow_void) {
    return (allow_void && minic_type_is_void(type)) ||
           minic_type_is_pointer(type) ||
           (minic_type_is_integer(type) && !minic_type_is_int128_integer(type));
}
bool minic_riscv_isa_lp64_integer_signature_supported(
    MinicType return_type, const MinicType *parameter_types,
    size_t parameter_count, bool is_variadic) {
    size_t i;
    if (is_variadic ||
        !lp64_single_integer_slot(return_type, true) ||
        (parameter_count != 0U && parameter_types == NULL))
        return false;
    for (i = 0U; i < parameter_count; ++i)
        if (!lp64_single_integer_slot(parameter_types[i], false))
            return false;
    return true;
}

static bool base_i_opcode(const char *op) {
    static const char *const allowed[] = {
        "add", "addi", "addiw", "addw", "and", "andi", "auipc",
        "beq", "bge", "bgeu", "blt", "bltu", "bne",
        "ebreak", "ecall", "fence", "jal", "jalr",
        "lb", "lbu", "ld", "lh", "lhu", "lui", "lw", "lwu",
        "or", "ori", "sb", "sd", "sh", "sll", "slli", "slliw",
        "sllw", "slt", "slti", "sltiu", "sltu",
        "sra", "srai", "sraiw", "sraw",
        "srl", "srli", "srliw", "srlw", "sub", "subw", "sw",
        "xor", "xori",
        /* Checked GNU assembler pseudos, validated again by GNU AS. */
        "beqz", "bgez", "bgt", "bgtu", "bgtz", "ble", "bleu",
        "blez", "bltz", "bnez", "call", "j", "jr", "la", "lla",
        "li", "mv", "neg", "negw", "nop", "not", "ret", "seqz",
        "sext.b", "sext.h", "sext.w", "sgtz", "sltz", "snez",
        "tail", "zext.b", "zext.h", "zext.w"
    };
    size_t i;
    for (i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(op, allowed[i]) == 0) return true;
    return false;
}

/* One capability mapping for ALL RV64 emitter families, including helper,
 * frame, call and FP opcodes. Unknown opcodes fail closed. Future explicit
 * feature support belongs here until a machine-IR selector is justified. */
static uint32_t required_features(const char *op, const char **description) {
    static const char *const m_ops[] = {
        "mul", "mulh", "mulhu", "mulhsu", "mulw", "div", "divu",
        "divw", "divuw", "rem", "remu", "remw", "remuw"
    };
    size_t i;
    if (base_i_opcode(op)) {
        *description = NULL;
        return 0U;
    }
    for (i = 0U; i < sizeof(m_ops) / sizeof(m_ops[0]); ++i) {
        if (strcmp(op, m_ops[i]) == 0) {
            *description = "M";
            return MINIC_RISCV_FEATURE_M;
        }
    }
    if (strncmp(op, "amo", 3) == 0 ||
        strncmp(op, "lr.", 3) == 0 || strncmp(op, "sc.", 3) == 0) {
        *description = "A";
        return MINIC_RISCV_FEATURE_A;
    }
    if (strcmp(op, "fence.i") == 0) {
        *description = "Zifencei";
        return MINIC_RISCV_FEATURE_ZIFENCEI;
    }
    if (strncmp(op, "csr", 3) == 0 ||
        strcmp(op, "rdcycle") == 0 || strcmp(op, "rdtime") == 0 ||
        strcmp(op, "rdinstret") == 0) {
        *description = "Zicsr";
        return MINIC_RISCV_FEATURE_ZICSR;
    }
    if (op[0] == 'c' && op[1] == '.') {
        *description = "C";
        return MINIC_RISCV_FEATURE_C;
    }
    if (op[0] == 'f' && op[1] != '\0') {
        /* The currently qualified FP backend emits only F/D families.
         * D opcodes use double suffix/operand; also require base F. */
        if (strstr(op, ".d") != NULL || strcmp(op, "fld") == 0 ||
            strcmp(op, "fsd") == 0) {
            *description = "F/D";
            return MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D;
        }
        *description = "F";
        return MINIC_RISCV_FEATURE_F;
    }
    *description = "unqualified machine opcode";
    return UINT32_MAX;
}

/* .insn, macro and ISA-setting directives may bypass a normal -march
 * check; reject these for all explicit profiles. User-provided arbitrary
 * inline asm is already rejected before reaching this audit. */
static bool escape_directive(const char *p) {
    return strncmp(p, ".insn", 5) == 0 ||
           strncmp(p, ".macro", 6) == 0 ||
           strncmp(p, ".rept", 5) == 0 ||
           (strncmp(p, ".option", 7) == 0 &&
            (strstr(p, "arch") != NULL || strstr(p, "rvc") != NULL)) ||
           strncmp(p, ".attribute arch", 15) == 0;
}

bool minic_riscv64_check_isa_assembly(const char *path,
                                      const MinicRiscvIsa *isa,
                                      char *reason, size_t capacity) {
    FILE *in;
    char *line = NULL;
    size_t allocated = 0U, lineno = 0U;
    ssize_t n;
    bool valid = true;
    if (isa == NULL || !isa->strict_isa)
        return reject(reason, capacity, "strict RISC-V ISA profile required");
    in = fopen(path, "rb");
    if (in == NULL)
        return reject(reason, capacity, "cannot audit RISC-V assembly");
    while ((n = getline(&line, &allocated, in)) >= 0) {
        char *p = line, op[96];
        const char *requirement;
        uint32_t needed;
        size_t length = 0U;
        (void)n;
        ++lineno;
        while (isspace((unsigned char)*p)) ++p;
        if (*p == '\0' || *p == '#' || *p == ';') continue;
        if (*p == '.') {
            if (escape_directive(p)) {
                if (reason != NULL && capacity != 0U)
                    (void)snprintf(reason, capacity,
                                   "RISC-V ISA line %zu: unverified assembler escape: %.48s",
                                   lineno, p);
                valid = false;
                break;
            }
            continue;
        }
        /* Local and numeric labels may prefix an actual opcode. */
        while (*p != '\0') {
            char *end = p;
            while (*end != '\0' && !isspace((unsigned char)*end) &&
                   *end != ':' && *end != '#') ++end;
            if (*end != ':') break;
            p = end + 1;
            while (isspace((unsigned char)*p)) ++p;
        }
        if (*p == '\0' || *p == '#' || *p == ';') continue;
        while (p[length] != '\0' && !isspace((unsigned char)p[length]) &&
               p[length] != '#' && p[length] != ';' &&
               length + 1U < sizeof(op)) {
            op[length] = p[length];
            ++length;
        }
        op[length] = '\0';
        if (length == 0U) continue;
        needed = required_features(op, &requirement);
        if (needed == UINT32_MAX || !minic_riscv_isa_has(isa, needed)) {
            if (reason != NULL && capacity != 0U)
                (void)snprintf(reason, capacity,
                               "RISC-V ISA line %zu: instruction '%s' requires %s; "
                               "selected features 0x%x; software lowering not qualified",
                               lineno, op, requirement, (unsigned)isa->features);
            valid = false;
            break;
        }
    }
    if (ferror(in))
        valid = reject(reason, capacity, "cannot read RISC-V assembly");
    free(line);
    (void)fclose(in);
    return valid;
}
