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
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
        if (minic_type_is_int128_integer(instruction->type))
            return reject(reason, capacity,
                          "__int128 operation has no qualified two-limb lowering");
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        /* Core integer constants use one signed 64-bit immediate; RV64I
         * materializes its 128-bit sign-extension in a second integer slot. */
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        /* These all have two-limb RV64I implementations in scalar_emit.c. */
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        /* Adding M must never remove a C operation admitted on Base-I.
         * A two-word compare has no native M instruction; strict profiles
         * select the same compiler-rt pair comparison on I and IM. */
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
        /* <=64-bit RV64I uses __muldi3; 128-bit uses upstream
         * compiler-rt __multi3 with integer-register LP64 calling ABI. */
        if (!minic_type_is_int128_integer(instruction->type) ||
            !minic_riscv_codegen_has(isa, MINIC_RISCV_FEATURE_M))
            return true;
        required = MINIC_RISCV_FEATURE_M;
        machine = "mulhu";
        extension = "M";
        break;
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
        /* Wide division/remainder uses compiler-rt TI libcalls, ordinary
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
        if (!minic_riscv_codegen_has(isa, MINIC_RISCV_FEATURE_F) &&
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
        if (!minic_riscv_codegen_has(
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
        /* FIXME(MC-053): LP64 softfloat supports the arithmetic opcodes
         * above, but these comparisons/conversions/negation still lack
         * corresponding libgcc+ABI lowering and must remain fail-closed. */
        required = MINIC_RISCV_FEATURE_F | MINIC_RISCV_FEATURE_D;
        machine = "fadd.d";
        extension = "F/D";
        break;
    default: break;
    }
    if (required == 0U || minic_riscv_codegen_has(isa, required)) return true;
    /* Distinguish the exact failing Core opcode, not merely the FP ISA
     * family used as a diagnostic placeholder. This is useful when a
     * compiler-rt translation unit contains many inline helper functions. */
    if (getenv("MINIC_RV64I_CORE_REJECT_TRACE") != NULL) {
        (void)fprintf(stderr,
            "RV64I_CORE_REJECT kind=%d base=%d sign=%d rank=%d "
            "result=%u machine=%s\n",
            (int)instruction->kind, (int)instruction->type.base_kind,
            (int)instruction->type.integer_sign,
            (int)instruction->type.integer_rank,
            (unsigned)instruction->result,
            machine == NULL ? "(none)" : machine);
        (void)minic_core_function_dump(stderr, function);
    }
    if (reason != NULL && capacity > 0U)
        (void)snprintf(reason, capacity,
                       "RISC-V instruction '%s' requires %s; selected ISA lacks "
                       "hardware lowering and software lowering is not qualified",
                       machine, extension);
    return false;
}

/* Provenance is part of the ordinary assembly format, not an opt-in mode.
 * Source-owned GNU asm is passed through after Core validates operand
 * transport. RISC-V's compiler-emitted ISA is audited separately from it.
 * Markers are nonexecuting assembler comments, preserving ELF content. */
bool minic_riscv64_source_asm_marker_safe(const char *text, size_t length) {
    static const char *const reserved[] = {
        "# MINIC_RV64I_ASM_BEGIN", "# MINIC_RV64I_ASM_END"
    };
    size_t i, j;
    if (text == NULL) return false;
    /* Source-owned comments share the assembler namespace. Never permit
     * them to spoof the exact delimiters that the machine audit trusts. */
    for (j = 0U; j < sizeof(reserved) / sizeof(reserved[0]); ++j) {
        size_t marker_length = strlen(reserved[j]);
        if (length < marker_length) continue;
        for (i = 0U; i <= length - marker_length; ++i)
            if (memcmp(text + i, reserved[j], marker_length) == 0)
                return false;
    }
    return true;
}

bool minic_riscv64_asm_audit_marker(FILE *file, bool begin) {
    return file != NULL && fprintf(file, "# MINIC_RV64I_ASM_%s\n",
                                   begin ? "BEGIN" : "END") >= 0;
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
 * check when produced by MiniC. User-supplied directives inside a marked
 * source region remain the assembler's responsibility. */
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
    bool in_user_asm = false;
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
        if (strncmp(p, "# MINIC_RV64I_ASM_BEGIN", 23) == 0 &&
            (p[23] == '\n' || p[23] == '\0')) {
            if (in_user_asm) {
                valid = reject(reason, capacity, "nested inline asm provenance markers");
                break;
            }
            in_user_asm = true;
            continue;
        }
        if (strncmp(p, "# MINIC_RV64I_ASM_END", 21) == 0 &&
            (p[21] == '\n' || p[21] == '\0')) {
            if (!in_user_asm) {
                valid = reject(reason, capacity, "unpaired inline asm end marker");
                break;
            }
            in_user_asm = false;
            continue;
        }
        if (in_user_asm) continue;
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
        if (needed == UINT32_MAX || !minic_riscv_codegen_has(isa, needed)) {
            if (reason != NULL && capacity != 0U)
                (void)snprintf(reason, capacity,
                               "RISC-V ISA line %zu: instruction '%s' requires %s; "
                               "compiler features 0x%x; software lowering not qualified",
                               lineno, op, requirement, (unsigned)isa->codegen_features);
            valid = false;
            break;
        }
    }
    if (in_user_asm)
        valid = reject(reason, capacity, "unclosed inline asm provenance region");
    if (ferror(in))
        valid = reject(reason, capacity, "cannot read RISC-V assembly");
    free(line);
    (void)fclose(in);
    return valid;
}
