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

bool minic_riscv_isa_resolve(const char *march, const char *mabi,
                             MinicRiscvIsa *isa, char *reason, size_t capacity) {
    if (isa == NULL || isa->xlen != 64U)
        return reject(reason, capacity, "only the existing RV64 backend is available");
    if (march == NULL) {
        if (mabi == NULL || strcmp(mabi, "lp64d") == 0) return true;
        return reject(reason, capacity,
                      "use -march=rv64i -mabi=lp64 for soft-float RV64I");
    }
    if (strcmp(march, "rv64i") != 0)
        return reject(reason, capacity,
                      "only -march=rv64i is qualified; omit -march for the existing default");
    if (mabi != NULL && strcmp(mabi, "lp64") != 0)
        return reject(reason, capacity, "rv64i requires -mabi=lp64 (lp64d requires D)");
    isa->xlen = 64U;
    isa->abi = MINIC_RISCV_ABI_LP64;
    isa->strict_base_i = true;
    return true;
}

/* These are actual RV64I opcodes and verified GNU assembler pseudos, not the
 * full RISC-V database. Only the strict opt-in path does this cheap audit.
 * Unknown instructions fail closed rather than being silently permitted. */
static bool base_i_opcode(const char *op) {
    static const char *const allowed[] = {
        "add", "addi", "addiw", "addw", "and", "andi", "auipc",
        "beq", "bge", "bgeu", "blt", "bltu", "bne",
        "ebreak", "ecall", "fence", "jal", "jalr",
        "lb", "lbu", "ld", "lh", "lhu", "lui", "lw", "lwu",
        "or", "ori", "sb", "sd", "sh", "sll", "slli", "slliw",
        "sllw", "slt", "slti", "sltiu", "sltu",
        "sra", "srai", "sraiw", "sraw", "srl", "srli", "srliw",
        "srlw", "sub", "subw", "sw", "xor", "xori",
        /* GNU assembler pseudo-instructions; AS -march=rv64i still verifies
         * their fully expanded instructions. */
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

static const char *extension_for(const char *op) {
    static const char *const m_ops[] = {
        "mul", "mulh", "mulhu", "mulhsu", "mulw", "div", "divu",
        "divw", "divuw", "rem", "remu", "remw", "remuw"
    };
    size_t i;
    for (i = 0U; i < sizeof(m_ops) / sizeof(m_ops[0]); ++i)
        if (strcmp(op, m_ops[i]) == 0) return "M";
    if (op[0] == 'f' && op[1] != '\0') return "F/D";
    if (strncmp(op, "amo", 3) == 0 || strncmp(op, "lr.", 3) == 0 ||
        strncmp(op, "sc.", 3) == 0) return "A";
    if (op[0] == 'c' && op[1] == '.') return "C";
    if (strncmp(op, "csr", 3) == 0) return "Zicsr";
    if (strcmp(op, "fence.i") == 0) return "Zifencei";
    return "non-RV64I";
}

bool minic_riscv64_check_base_i_assembly(const char *path, char *reason,
                                          size_t capacity) {
    FILE *in = fopen(path, "rb");
    char *line = NULL;
    size_t allocated = 0U, lineno = 0U;
    ssize_t n;
    bool valid = true;
    if (in == NULL) return reject(reason, capacity, "cannot audit RV64I assembly");
    while ((n = getline(&line, &allocated, in)) >= 0) {
        char *p = line;
        char op[96];
        size_t length = 0U;
        (void)n;
        ++lineno;
        while (isspace((unsigned char)*p)) ++p;
        if (*p == '\0' || *p == '#' || *p == ';') continue;
        /* A strict target cannot let user ASM re-enable disabled extensions.
         * Ordinary data directives remain necessary for constant objects. */
        if (*p == '.') {
            if (strncmp(p, ".insn", 5) == 0 ||
                strncmp(p, ".macro", 6) == 0 ||
                strncmp(p, ".rept", 5) == 0 ||
                (strncmp(p, ".option", 7) == 0 &&
                 (strstr(p, "arch") != NULL || strstr(p, "rvc") != NULL))) {
                if (reason != NULL && capacity > 0U)
                    (void)snprintf(reason, capacity,
                                   "rv64i line %zu: unverified assembler escape: %.50s",
                                   lineno, p);
                valid = false;
                break;
            }
            continue;
        }
        /* Skip label prefixes (including local numeric labels). */
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
        if (!base_i_opcode(op)) {
            if (reason != NULL && capacity > 0U)
                (void)snprintf(reason, capacity,
                               "rv64i line %zu: instruction '%s' requires %s; "
                               "software lowering not qualified",
                               lineno, op, extension_for(op));
            valid = false;
            break;
        }
    }
    if (ferror(in)) valid = reject(reason, capacity, "cannot read RV64I assembly");
    free(line);
    (void)fclose(in);
    return valid;
}
