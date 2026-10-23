#define _POSIX_C_SOURCE 200809L
#include "target/riscv64/isa.h"

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
