#include "minias_internal.h"
#include "riscv_fixup_kinds.h"
#include "riscv_target.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int reg_number(const char *name) {
    static const struct {
        const char *name;
        int number;
    } regs[] = {
        {"zero", 0}, {"ra", 1}, {"sp", 2}, {"gp", 3}, {"tp", 4},
        {"t0", 5}, {"t1", 6}, {"t2", 7}, {"s0", 8}, {"fp", 8}, {"s1", 9},
        {"a0", 10}, {"a1", 11}, {"a2", 12}, {"a3", 13}, {"a4", 14},
        {"a5", 15}, {"a6", 16}, {"a7", 17}, {"s2", 18}, {"s3", 19},
        {"s4", 20}, {"s5", 21}, {"s6", 22}, {"s7", 23}, {"s8", 24},
        {"s9", 25}, {"s10", 26}, {"s11", 27}, {"t3", 28}, {"t4", 29},
        {"t5", 30}, {"t6", 31},
    };
    size_t i;
    char *end = NULL;
    long value;

    if (strcmp(name, "0") == 0) {
        return 0;
    }
    if (name[0] == 'x') {
        errno = 0;
        value = strtol(name + 1, &end, 10);
        if (errno == 0 && end != name + 1 && *end == '\0' && value >= 0 &&
            value <= 31) {
            return (int)value;
        }
    }
    for (i = 0U; i < sizeof(regs) / sizeof(regs[0]); ++i) {
        if (strcmp(name, regs[i].name) == 0) {
            return regs[i].number;
        }
    }
    return -1;
}

static int float_reg_number(const char *name) {
    static const struct {
        const char *name;
        int number;
    } aliases[] = {
        {"ft0", 0},  {"ft1", 1},  {"ft2", 2},  {"ft3", 3},
        {"ft4", 4},  {"ft5", 5},  {"ft6", 6},  {"ft7", 7},
        {"fs0", 8},  {"fs1", 9},
        {"fa0", 10}, {"fa1", 11}, {"fa2", 12}, {"fa3", 13},
        {"fa4", 14}, {"fa5", 15}, {"fa6", 16}, {"fa7", 17},
        {"fs2", 18}, {"fs3", 19}, {"fs4", 20}, {"fs5", 21},
        {"fs6", 22}, {"fs7", 23}, {"fs8", 24}, {"fs9", 25},
        {"fs10", 26}, {"fs11", 27},
        {"ft8", 28}, {"ft9", 29}, {"ft10", 30}, {"ft11", 31},
    };
    char *end = NULL;
    long value;
    size_t index;

    if (name == NULL) {
        return -1;
    }
    if (name[0] == 'f' && isdigit((unsigned char)name[1])) {
        errno = 0;
        value = strtol(name + 1, &end, 10);
        if (errno == 0 && end != name + 1 && *end == '\0' &&
            value >= 0 && value <= 31) {
            return (int)value;
        }
    }
    for (index = 0U; index < sizeof(aliases) / sizeof(aliases[0]); ++index) {
        if (strcmp(name, aliases[index].name) == 0) {
            return aliases[index].number;
        }
    }
    return -1;
}

static bool parse_fp_rounding_mode(const char *text, uint32_t *mode) {
    static const struct {
        const char *name;
        uint32_t value;
    } modes[] = {
        {"rne", 0U}, {"rtz", 1U}, {"rdn", 2U},
        {"rup", 3U}, {"rmm", 4U}, {"dyn", 7U},
    };
    size_t index;

    if (text == NULL || mode == NULL) {
        return false;
    }
    for (index = 0U; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        if (strcmp(text, modes[index].name) == 0) {
            *mode = modes[index].value;
            return true;
        }
    }
    return false;
}

static bool is_fp_binary_single(const char *op) {
    return strcmp(op, "fadd.s") == 0 || strcmp(op, "fsub.s") == 0 ||
           strcmp(op, "fmul.s") == 0 || strcmp(op, "fdiv.s") == 0;
}

static bool is_fp_binary_double(const char *op) {
    return strcmp(op, "fadd.d") == 0 || strcmp(op, "fsub.d") == 0 ||
           strcmp(op, "fmul.d") == 0 || strcmp(op, "fdiv.d") == 0;
}

static bool is_fp_compare_double(const char *op) {
    return strcmp(op, "feq.d") == 0 || strcmp(op, "flt.d") == 0 ||
           strcmp(op, "fle.d") == 0;
}

static bool is_fp_convert(const char *op) {
    return strcmp(op, "fcvt.d.w") == 0 || strcmp(op, "fcvt.d.wu") == 0 ||
           strcmp(op, "fcvt.d.l") == 0 || strcmp(op, "fcvt.d.lu") == 0 ||
           strcmp(op, "fcvt.w.d") == 0 || strcmp(op, "fcvt.wu.d") == 0 ||
           strcmp(op, "fcvt.l.d") == 0 || strcmp(op, "fcvt.lu.d") == 0 ||
           strcmp(op, "fcvt.d.s") == 0 || strcmp(op, "fcvt.s.d") == 0;
}

static bool fp_convert_int_to_double(const char *op) {
    return strcmp(op, "fcvt.d.w") == 0 || strcmp(op, "fcvt.d.wu") == 0 ||
           strcmp(op, "fcvt.d.l") == 0 || strcmp(op, "fcvt.d.lu") == 0;
}

static bool fp_convert_double_to_int(const char *op) {
    return strcmp(op, "fcvt.w.d") == 0 || strcmp(op, "fcvt.wu.d") == 0 ||
           strcmp(op, "fcvt.l.d") == 0 || strcmp(op, "fcvt.lu.d") == 0;
}

static int vector_reg_number(const char *name) {
    char *end = NULL;
    long value;

    if (name == NULL || name[0] != 'v' || !isdigit((unsigned char)name[1])) {
        return -1;
    }
    errno = 0;
    value = strtol(name + 1, &end, 10);
    if (errno != 0 || end == name + 1 || *end != '\0' || value < 0 || value > 31) {
        return -1;
    }
    return (int)value;
}

static bool decode_vector_unit_stride(const char *op,
                                      bool *is_store,
                                      uint32_t *width_funct3) {
    const char *prefix;
    const char *width_text;

    if (op == NULL || is_store == NULL || width_funct3 == NULL) {
        return false;
    }
    if (strncmp(op, "vle", 3U) == 0) {
        *is_store = false;
        prefix = "vle";
    } else if (strncmp(op, "vse", 3U) == 0) {
        *is_store = true;
        prefix = "vse";
    } else {
        return false;
    }
    width_text = op + strlen(prefix);
    if (strcmp(width_text, "8.v") == 0) {
        *width_funct3 = 0U;
    } else if (strcmp(width_text, "16.v") == 0) {
        *width_funct3 = 5U;
    } else if (strcmp(width_text, "32.v") == 0) {
        *width_funct3 = 6U;
    } else if (strcmp(width_text, "64.v") == 0) {
        *width_funct3 = 7U;
    } else {
        return false;
    }
    return true;
}

/* Integer constant expressions are parsed by the shared assembler
 * evaluator; no second target-specific grammar or precedence table. */
static bool parse_i64(const char *text, int64_t *value) {
    uint64_t bits;

    if (!minias_eval_u64_expression(text, &bits)) {
        return false;
    }
    *value = (int64_t)bits;
    return true;
}

static bool parse_li_bits(const char *text,
                          uint64_t *bits,
                          bool *fits_signed32,
                          int64_t *signed_value) {
    char *end = NULL;

    if (text == NULL || bits == NULL || fits_signed32 == NULL ||
        signed_value == NULL) {
        return false;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    if (*text == '-') {
        long long parsed;
        errno = 0;
        parsed = strtoll(text, &end, 0);
        if (errno == 0 && end != text) {
            while (*end == ' ' || *end == '\t') {
                ++end;
            }
            if (*end == '\0') {
                *signed_value = (int64_t)parsed;
                *bits = (uint64_t)*signed_value;
                *fits_signed32 =
                    *signed_value >= INT32_MIN && *signed_value <= INT32_MAX;
                return true;
            }
        }
    } else {
        unsigned long long parsed;
        errno = 0;
        parsed = strtoull(text, &end, 0);
        if (errno == 0 && end != text) {
            while (*end == ' ' || *end == '\t') {
                ++end;
            }
            if (*end == '\0') {
                *bits = (uint64_t)parsed;
                if (*bits <= (uint64_t)INT64_MAX) {
                    *signed_value = (int64_t)*bits;
                    *fits_signed32 =
                        *signed_value >= INT32_MIN && *signed_value <= INT32_MAX;
                } else {
                    *signed_value = 0;
                    *fits_signed32 = false;
                }
                return true;
            }
        }
    }

    if (!minias_eval_u64_expression(text, bits)) {
        return false;
    }
    *signed_value = (int64_t)*bits;
    *fits_signed32 =
        *signed_value >= INT32_MIN && *signed_value <= INT32_MAX;
    return true;
}

/* RV32 li accepts the exact 32-bit bit pattern or its 64-bit sign extension.
 * The existing RV64 pseudo expansion is untouched. This keeps first-pass
 * label layout in agreement with encoding and forbids silent truncation. */
static bool rv32_li_value(uint64_t bits, int64_t *result) {
    uint32_t narrow = (uint32_t)bits;
    int64_t signed_value = (narrow & UINT32_C(0x80000000)) ?
        (int64_t)narrow - INT64_C(4294967296) : (int64_t)narrow;
    if (result == NULL ||
        (bits != (uint64_t)narrow && bits != (uint64_t)signed_value))
        return false;
    *result = signed_value;
    return true;
}

static uint32_t li64_materialized_size(uint64_t bits) {
    int byte_index = 7;
    uint32_t instructions = 1U;
    int i;

    while (byte_index > 0 &&
           ((bits >> ((unsigned int)byte_index * 8U)) & UINT64_C(0xff)) == 0U) {
        --byte_index;
    }
    for (i = byte_index - 1; i >= 0; --i) {
        uint64_t byte = (bits >> ((unsigned int)i * 8U)) & UINT64_C(0xff);
        ++instructions;
        if (byte != 0U) {
            ++instructions;
        }
    }
    return instructions * 4U;
}

static bool parse_csr_numeric_expression(const char *text, uint32_t *csr) {
    const char *cursor = text;
    char *end = NULL;
    unsigned long long result;

    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }
    errno = 0;
    result = strtoull(cursor, &end, 0);
    if (errno != 0 || end == cursor) {
        return false;
    }
    cursor = end;

    for (;;) {
        char op;
        unsigned long long term;

        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (*cursor == '\0') {
            if (result > 0xfffULL) {
                return false;
            }
            *csr = (uint32_t)result;
            return true;
        }
        op = *cursor;
        if (op != '+' && op != '-') {
            return false;
        }
        ++cursor;
        while (*cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        errno = 0;
        term = strtoull(cursor, &end, 0);
        if (errno != 0 || end == cursor) {
            return false;
        }
        if (op == '+') {
            if (term > UINT64_MAX - result) {
                return false;
            }
            result += term;
        } else {
            if (term > result) {
                return false;
            }
            result -= term;
        }
        cursor = end;
    }
}

static bool parse_csr(const char *text, uint32_t *csr) {
    static const struct {
        const char *name;
        uint32_t value;
    } names[] = {
        {"fflags", 0x001U}, {"frm", 0x002U}, {"fcsr", 0x003U},
        {"sstatus", 0x100U}, {"sie", 0x104U}, {"stvec", 0x105U},
        {"sscratch", 0x140U}, {"sepc", 0x141U}, {"scause", 0x142U},
        {"stval", 0x143U}, {"sip", 0x144U}, {"satp", 0x180U},
        {"mstatus", 0x300U}, {"mtvec", 0x305U}, {"mscratch", 0x340U},
        {"mepc", 0x341U}, {"mcause", 0x342U}, {"mtval", 0x343U},
        {"mip", 0x344U}, {"cycle", 0xc00U}, {"time", 0xc01U},
        {"instret", 0xc02U}, {"mhartid", 0xf14U},
    };
    size_t i;

    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(text, names[i].name) == 0) {
            *csr = names[i].value;
            return true;
        }
    }
    return parse_csr_numeric_expression(text, csr);
}

static bool parse_fence_set(const char *text, uint32_t *mask) {
    uint32_t value = 0U;
    const char *p;

    if (text == NULL || *text == '\0') {
        return false;
    }
    for (p = text; *p != '\0'; ++p) {
        if (*p == 'i') {
            value |= 8U;
        } else if (*p == 'o') {
            value |= 4U;
        } else if (*p == 'r') {
            value |= 2U;
        } else if (*p == 'w') {
            value |= 1U;
        } else if (*p != ' ' && *p != '\t') {
            return false;
        }
    }
    *mask = value;
    return true;
}


static bool decode_amo_mnemonic(const char *op,
                                uint32_t *funct5,
                                uint32_t *width_funct3,
                                uint32_t *ordering_bits) {
    char core[64];
    size_t n;
    const char *suffix = NULL;

    if (op == NULL || funct5 == NULL || width_funct3 == NULL ||
        ordering_bits == NULL) {
        return false;
    }
    n = strlen(op);
    if (n == 0U || n >= sizeof(core)) {
        return false;
    }
    memcpy(core, op, n + 1U);

    *ordering_bits = 0U;
    if (n > 5U && strcmp(core + n - 5U, ".aqrl") == 0) {
        core[n - 5U] = '\0';
        *ordering_bits = (1U << 26U) | (1U << 25U);
    } else if (n > 3U && strcmp(core + n - 3U, ".aq") == 0) {
        core[n - 3U] = '\0';
        *ordering_bits = 1U << 26U;
    } else if (n > 3U && strcmp(core + n - 3U, ".rl") == 0) {
        core[n - 3U] = '\0';
        *ordering_bits = 1U << 25U;
    }

    n = strlen(core);
    if (n < 3U || core[n - 2U] != '.') {
        return false;
    }
    suffix = core + n - 2U;
    if (strcmp(suffix, ".w") == 0) {
        *width_funct3 = 2U;
    } else if (strcmp(suffix, ".d") == 0) {
        *width_funct3 = 3U;
    } else {
        return false;
    }
    core[n - 2U] = '\0';

    if (strcmp(core, "amoadd") == 0) {
        *funct5 = 0x00U;
    } else if (strcmp(core, "amoswap") == 0) {
        *funct5 = 0x01U;
    } else if (strcmp(core, "amoxor") == 0) {
        *funct5 = 0x04U;
    } else if (strcmp(core, "amoor") == 0) {
        *funct5 = 0x08U;
    } else if (strcmp(core, "amoand") == 0) {
        *funct5 = 0x0cU;
    } else if (strcmp(core, "amomin") == 0) {
        *funct5 = 0x10U;
    } else if (strcmp(core, "amomax") == 0) {
        *funct5 = 0x14U;
    } else if (strcmp(core, "amominu") == 0) {
        *funct5 = 0x18U;
    } else if (strcmp(core, "amomaxu") == 0) {
        *funct5 = 0x1cU;
    } else {
        return false;
    }
    return true;
}

static bool decode_lr_sc_mnemonic(const char *op,
                                  bool *is_lr,
                                  uint32_t *width_funct3,
                                  uint32_t *ordering_bits) {
    char core[32];
    size_t n;
    const char *suffix;

    if (op == NULL || is_lr == NULL || width_funct3 == NULL ||
        ordering_bits == NULL) {
        return false;
    }
    n = strlen(op);
    if (n == 0U || n >= sizeof(core)) {
        return false;
    }
    memcpy(core, op, n + 1U);

    *ordering_bits = 0U;
    if (n > 5U && strcmp(core + n - 5U, ".aqrl") == 0) {
        core[n - 5U] = '\0';
        *ordering_bits = (1U << 26U) | (1U << 25U);
    } else if (n > 3U && strcmp(core + n - 3U, ".aq") == 0) {
        core[n - 3U] = '\0';
        *ordering_bits = 1U << 26U;
    } else if (n > 3U && strcmp(core + n - 3U, ".rl") == 0) {
        core[n - 3U] = '\0';
        *ordering_bits = 1U << 25U;
    }

    n = strlen(core);
    if (n < 3U || core[n - 2U] != '.') {
        return false;
    }
    suffix = core + n - 2U;
    if (strcmp(suffix, ".w") == 0) {
        *width_funct3 = 2U;
    } else if (strcmp(suffix, ".d") == 0) {
        *width_funct3 = 3U;
    } else {
        return false;
    }
    core[n - 2U] = '\0';

    if (strcmp(core, "lr") == 0) {
        *is_lr = true;
        return true;
    }
    if (strcmp(core, "sc") == 0) {
        *is_lr = false;
        return true;
    }
    return false;
}

static size_t split_operands(const char *args, char out[][128], size_t max_out) {
    size_t count = 0U;
    size_t pos = 0U;
    int depth = 0;
    char quote = '\0';
    const char *p;

    if (args == NULL || *args == '\0') {
        return 0U;
    }
    memset(out, 0, max_out * 128U);
    for (p = args;; ++p) {
        char ch = *p;
        bool at_end = ch == '\0';

        if (!at_end && quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
        } else if (!at_end && (ch == '\'' || ch == '"')) {
            quote = ch;
        } else if (!at_end && ch == '(') {
            ++depth;
        } else if (!at_end && ch == ')') {
            --depth;
        }
        if (at_end || (ch == ',' && depth == 0 && quote == '\0')) {
            while (pos > 0U &&
                   (out[count][pos - 1U] == ' ' || out[count][pos - 1U] == '\t')) {
                --pos;
            }
            out[count][pos] = '\0';
            ++count;
            pos = 0U;
            if (at_end || count == max_out) {
                break;
            }
            continue;
        }
        if (count < max_out && pos + 1U < 128U) {
            if (pos == 0U && (ch == ' ' || ch == '\t')) {
                continue;
            }
            out[count][pos++] = ch;
        }
    }
    return count;
}

static uint32_t enc_r(uint32_t opcode,
                      int rd,
                      uint32_t funct3,
                      int rs1,
                      int rs2,
                      uint32_t funct7) {
    return opcode | ((uint32_t)rd << 7U) | (funct3 << 12U) |
           ((uint32_t)rs1 << 15U) | ((uint32_t)rs2 << 20U) | (funct7 << 25U);
}

static uint32_t enc_i(uint32_t opcode, int rd, uint32_t funct3, int rs1, int64_t imm) {
    return opcode | ((uint32_t)rd << 7U) | (funct3 << 12U) |
           ((uint32_t)rs1 << 15U) | (((uint32_t)imm & 0xfffU) << 20U);
}

static uint32_t enc_b(uint32_t funct3, int rs1, int rs2, int64_t off) {
    uint32_t value = (uint32_t)off & 0x1fffU;

    return 0x63U | (((value >> 11U) & 1U) << 7U) |
           (((value >> 1U) & 0xfU) << 8U) | (funct3 << 12U) |
           ((uint32_t)rs1 << 15U) | ((uint32_t)rs2 << 20U) |
           (((value >> 5U) & 0x3fU) << 25U) |
           (((value >> 12U) & 1U) << 31U);
}

static uint32_t enc_j(int rd, int64_t off) {
    uint32_t value = (uint32_t)off & 0x1fffffU;

    return 0x6fU | ((uint32_t)rd << 7U) |
           (((value >> 12U) & 0xffU) << 12U) |
           (((value >> 11U) & 1U) << 20U) |
           (((value >> 1U) & 0x3ffU) << 21U) |
           (((value >> 20U) & 1U) << 31U);
}

static bool append_u16(MiniAs *as, int section, uint16_t value) {
    unsigned char bytes[2];

    bytes[0] = (unsigned char)(value & 0xffU);
    bytes[1] = (unsigned char)((value >> 8U) & 0xffU);
    return minias_emit_instruction_bytes(as, section, bytes, sizeof(bytes));
}

static bool append_u32(MiniAs *as, int section, uint32_t value) {
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xffU);
    bytes[1] = (unsigned char)((value >> 8U) & 0xffU);
    bytes[2] = (unsigned char)((value >> 16U) & 0xffU);
    bytes[3] = (unsigned char)((value >> 24U) & 0xffU);
    return minias_emit_instruction_bytes(as, section, bytes, sizeof(bytes));
}

static bool parse_vsetvli_vtype(char operands[][128],
                                  size_t count,
                                  uint32_t *vtypei) {
    size_t index = 2U;
    uint32_t vsew;
    uint32_t vlmul = 0U;
    uint32_t vta;
    uint32_t vma;

    if (count != 5U && count != 6U) {
        return false;
    }
    if (strcmp(operands[index], "e8") == 0) {
        vsew = 0U;
    } else if (strcmp(operands[index], "e16") == 0) {
        vsew = 1U;
    } else if (strcmp(operands[index], "e32") == 0) {
        vsew = 2U;
    } else if (strcmp(operands[index], "e64") == 0) {
        vsew = 3U;
    } else {
        return false;
    }
    ++index;

    if (count == 6U) {
        if (strcmp(operands[index], "m1") == 0) {
            vlmul = 0U;
        } else if (strcmp(operands[index], "m2") == 0) {
            vlmul = 1U;
        } else if (strcmp(operands[index], "m4") == 0) {
            vlmul = 2U;
        } else if (strcmp(operands[index], "m8") == 0) {
            vlmul = 3U;
        } else if (strcmp(operands[index], "mf2") == 0) {
            vlmul = 7U;
        } else if (strcmp(operands[index], "mf4") == 0) {
            vlmul = 6U;
        } else if (strcmp(operands[index], "mf8") == 0) {
            vlmul = 5U;
        } else {
            return false;
        }
        ++index;
    }

    if (strcmp(operands[index], "ta") == 0) {
        vta = 1U;
    } else if (strcmp(operands[index], "tu") == 0) {
        vta = 0U;
    } else {
        return false;
    }
    ++index;

    if (strcmp(operands[index], "ma") == 0) {
        vma = 1U;
    } else if (strcmp(operands[index], "mu") == 0) {
        vma = 0U;
    } else {
        return false;
    }

    *vtypei = (vma << 7U) | (vta << 6U) | (vsew << 3U) | vlmul;
    return true;
}

static bool parse_mem(const char *text, int64_t *offset, char base[128]) {
    const char *left = strrchr(text, '(');
    const char *right = strrchr(text, ')');
    char offbuf[128];
    size_t n;

    if (left == NULL || right == NULL || right < left || right[1] != '\0') {
        return false;
    }
    n = (size_t)(left - text);
    if (n >= sizeof(offbuf) || (size_t)(right - left - 1) >= 128U) {
        return false;
    }
    memcpy(offbuf, text, n);
    offbuf[n] = '\0';
    memcpy(base, left + 1, (size_t)(right - left - 1));
    base[right - left - 1] = '\0';
    if (offbuf[0] == '\0') {
        strcpy(offbuf, "0");
    }
    return parse_i64(offbuf, offset);
}

static bool parse_raw_insn_directive(const char *args, uint32_t *word) {
    const char *p = args;
    char format;
    char operands[8][128];
    size_t count;
    int64_t opcode;
    int64_t funct3;
    int64_t funct7;
    int64_t immediate;
    int rd;
    int rs1;
    int rs2;

    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if ((p[0] != 'r' && p[0] != 'i') ||
        (p[1] != ' ' && p[1] != '\t')) {
        return false;
    }
    format = p[0];
    ++p;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    count = split_operands(p, operands, 8U);

    if (format == 'r') {
        if (count != 6U ||
            !parse_i64(operands[0], &opcode) || opcode < 0 || opcode > 0x7f ||
            !parse_i64(operands[1], &funct3) || funct3 < 0 || funct3 > 0x7 ||
            !parse_i64(operands[2], &funct7) || funct7 < 0 || funct7 > 0x7f) {
            return false;
        }
        rd = reg_number(operands[3]);
        rs1 = reg_number(operands[4]);
        rs2 = reg_number(operands[5]);
        if (rd < 0 || rs1 < 0 || rs2 < 0) {
            return false;
        }
        *word = (uint32_t)opcode |
                ((uint32_t)rd << 7U) |
                ((uint32_t)funct3 << 12U) |
                ((uint32_t)rs1 << 15U) |
                ((uint32_t)rs2 << 20U) |
                ((uint32_t)funct7 << 25U);
        return true;
    }

    if (count != 5U ||
        !parse_i64(operands[0], &opcode) || opcode < 0 || opcode > 0x7f ||
        !parse_i64(operands[1], &funct3) || funct3 < 0 || funct3 > 0x7 ||
        !parse_i64(operands[4], &immediate) ||
        immediate < -2048 || immediate > 2047) {
        return false;
    }
    rd = reg_number(operands[2]);
    rs1 = reg_number(operands[3]);
    if (rd < 0 || rs1 < 0) {
        return false;
    }
    *word = (uint32_t)opcode |
            ((uint32_t)rd << 7U) |
            ((uint32_t)funct3 << 12U) |
            ((uint32_t)rs1 << 15U) |
            (((uint32_t)immediate & 0xfffU) << 20U);
    return true;
}

static bool parse_call_target(const char *text, MiniAsSymbolExpr *expr) {
    char normalized[256];
    char *plt;
    char *suffix;
    size_t length;

    if (text == NULL || expr == NULL) {
        return false;
    }
    length = strlen(text);
    if (length >= sizeof(normalized)) {
        return false;
    }
    memcpy(normalized, text, length + 1U);

    plt = strstr(normalized, "@plt");
    if (plt != NULL) {
        suffix = plt + 4;
        if (*suffix != '\0' && *suffix != ' ' && *suffix != '\t' &&
            *suffix != '+' && *suffix != '-') {
            return false;
        }
        memmove(plt, suffix, strlen(suffix) + 1U);
    }
    return minias_parse_symbol_addend(normalized, expr);
}

/* Measure the actual persisted target instruction. The layout driver
 * invokes this once after operand parsing and before placement. Neither
 * symbol spelling nor source-level expression grammar is revisited. */
bool minias_riscv_measure_mcinst(MiniAs *as, const MiniAsStmt *stmt,
                                 uint32_t *size) {
    const MiniAsMCInst *m = stmt->mcinst;
    if (m == NULL) {
        minias_set_error(as, "unsupported-instruction:%s:line=%zu",
                         stmt->op, stmt->line);
        return false;
    }
    switch (m->opcode) {
        case RV_MC_C16:
            *size = 2U;
            return true;
        case RV_MC_SYMBOL_LOAD:
        case RV_MC_SYMBOL_STORE:
        case RV_MC_CALL:
        case RV_MC_TAIL:
        case RV_MC_LA:
            *size = 8U;
            return true;
        case RV_MC_LI: {
            uint64_t bits = m->operands[1].immediate;
            const int64_t signed_value = (int64_t)m->operands[2].immediate;
            const bool fits_signed32 = m->encoding != 0U;
            if (as->target_xlen == 32U) {
                int64_t value, hi, lo;
                if (!rv32_li_value(bits, &value)) {
                    minias_set_error(as,
                                     "rv32i-li-overflows-32-bits:line=%zu",
                                     stmt->line);
                    return false;
                }
                hi = (value + INT64_C(2048)) >> 12;
                lo = value - hi * INT64_C(4096);
                *size = (value >= -2048 && value <= 2047) ||
                        lo == 0 ? 4U : 8U;
            } else if (fits_signed32) {
                *size = signed_value >= -2048 && signed_value <= 2047 ?
                        4U : 8U;
            } else {
                *size = li64_materialized_size(bits);
            }
            return true;
        }
        default:
            *size = 4U;
            return true;
    }
}

/* RISC-V's first production MCInst family: fixed-width integer ALU,
 * memory, control transfer and selected ISA extensions. The descriptors
 * contain machine encoding templates but no source operands. */
typedef struct RvMCDesc {
    const char *name;
    unsigned family;
    uint32_t base;
} RvMCDesc;
#define RVD(name, group, base) {name, RV_MC_##group, base}
static const RvMCDesc rv_mc_table[] = {
    RVD("add",R,0x00000033U), RVD("sub",R,0x40000033U),
    RVD("sll",R,0x00001033U), RVD("slt",R,0x00002033U),
    RVD("sltu",R,0x00003033U), RVD("xor",R,0x00004033U),
    RVD("srl",R,0x00005033U), RVD("sra",R,0x40005033U),
    RVD("or",R,0x00006033U), RVD("and",R,0x00007033U),
    RVD("mul",R,0x02000033U), RVD("mulh",R,0x02001033U),
    RVD("mulhu",R,0x02003033U), RVD("div",R,0x02004033U),
    RVD("divu",R,0x02005033U), RVD("rem",R,0x02006033U),
    RVD("remu",R,0x02007033U),
    RVD("addw",R,0x0000003bU), RVD("subw",R,0x4000003bU),
    RVD("sllw",R,0x0000103bU), RVD("srlw",R,0x0000503bU),
    RVD("sraw",R,0x4000503bU),
    RVD("mulw",R,0x0200003bU), RVD("divw",R,0x0200403bU),
    RVD("divuw",R,0x0200503bU), RVD("remw",R,0x0200603bU),
    RVD("remuw",R,0x0200703bU),
    RVD("sgt",R,0x00002033U), RVD("sgtu",R,0x00003033U),
    RVD("addi",I,0x00000013U), RVD("addiw",I,0x0000001bU),
    RVD("slti",I,0x00002013U), RVD("sltiu",I,0x00003013U),
    RVD("xori",I,0x00004013U), RVD("ori",I,0x00006013U),
    RVD("andi",I,0x00007013U),
    RVD("slli",I,0x00001013U), RVD("srli",I,0x00005013U),
    RVD("srai",I,0x40005013U),
    RVD("slliw",I,0x0000101bU), RVD("srliw",I,0x0000501bU),
    RVD("sraiw",I,0x4000501bU),
    RVD("sext.w",I,0x0000001bU),
    RVD("orc.b",I,0x28705013U), RVD("rev8",I,0x6b805013U),
    RVD("ctz",I,0x60101013U),
    RVD("lb",LOAD,0x00000003U), RVD("lh",LOAD,0x00001003U),
    RVD("lw",LOAD,0x00002003U), RVD("ld",LOAD,0x00003003U),
    RVD("lbu",LOAD,0x00004003U), RVD("lhu",LOAD,0x00005003U),
    RVD("lwu",LOAD,0x00006003U),
    RVD("sb",STORE,0x00000023U), RVD("sh",STORE,0x00001023U),
    RVD("sw",STORE,0x00002023U), RVD("sd",STORE,0x00003023U),
    RVD("lui",U,0x00000037U), RVD("auipc",U,0x00000017U),
    RVD("beq",BRANCH,0x00000063U), RVD("bne",BRANCH,0x00001063U),
    RVD("blt",BRANCH,0x00004063U), RVD("bge",BRANCH,0x00005063U),
    RVD("bltu",BRANCH,0x00006063U), RVD("bgeu",BRANCH,0x00007063U),
    RVD("beqz",BRANCH,0x00000063U), RVD("bnez",BRANCH,0x00001063U),
    RVD("bltz",BRANCH,0x00004063U), RVD("bgez",BRANCH,0x00005063U),
    RVD("bgtz",BRANCH,0x00004063U), RVD("blez",BRANCH,0x00005063U),
    RVD("bgt",BRANCH,0x00004063U), RVD("ble",BRANCH,0x00005063U),
    RVD("bgtu",BRANCH,0x00006063U), RVD("bleu",BRANCH,0x00007063U),
    RVD("j",JAL,0x0000006fU), RVD("jal",JAL,0x0000006fU)
};
#undef RVD


/* A fixed-width typed instruction has no layout dependence on its operand
 * text. Its target parser validates operand shape after the 4-byte size is
 * assigned: this avoids decoding the same registers/immediates once during
 * measurement and again when creating MCInst.
 *
 * The non-typed pseudo expansions, symbolic GPR loads/stores and strict-I
 * machine-word verifier continue through the complete legacy measurement
 * path until their fragment sizing is also represented in the typed IR. */
static void rv_set_reg(MiniAsMCOperand *op, int num) {
    op->kind = MINIAS_MC_REGISTER;
    op->reg = (unsigned)num;
}
static void rv_set_imm(MiniAsMCOperand *op, int64_t num) {
    op->kind = MINIAS_MC_IMMEDIATE;
    op->immediate = (uint64_t)num;
}

/* Only this target knows operand shape; all GNU aliases stay in the
 * front-end parser, while encoder uses canonical rd/rs1/rs2/symbol fields.
 * Other opcode families intentionally remain on the legacy proven path. */

/* All of these instructions are one machine word. Parse target operands
 * once and retain individual architectural registers in MCInst; preserve
 * CSR numbers, vector vtype, and memory/AMO width in the opcode template.
 * No source string is accessed by the final instruction writer. */
static bool rv_mc_word(MiniAs *as, MiniAsStmt *stmt,
                       uint32_t skeleton, int rd, int rs1, int rs2) {
    MiniAsMCInst *m = calloc(1U, sizeof(*m) + 3U * sizeof(*m->operands));
    if (m == NULL) {
        minias_set_error(as, "out-of-memory:riscv-word-mcinst");
        return false;
    }
    m->opcode = RV_MC_WORD;
    m->encoding = skeleton;
    m->count = 3U;
    rv_set_reg(&m->operands[0], rd);
    rv_set_reg(&m->operands[1], rs1);
    rv_set_reg(&m->operands[2], rs2);
    stmt->mcinst = m;
    return true;
}

static bool rv_mc_vector_word(MiniAs *as, MiniAsStmt *stmt,
                               uint32_t base, int vd, int rs1) {
    if (!rv_mc_word(as, stmt, base, vd, rs1, 0)) return false;
    stmt->mcinst->operands[0].reg_class = MINIAS_MC_REG_VECTOR;
    return true;
}

/* Common integer, supervisor, CSR-alias and compressed pseudo instructions
 * use the same typed instruction ownership as real ISA instructions. Only
 * multiword position-dependent pseudos stay on the legacy fragment path. */
static bool rv_parse_simple_mc(MiniAs *as, MiniAsStmt *stmt,
                               char ops[][128], size_t n, bool *handled) {
    const char *op = stmt->op;
    int rd=0,rs1=0,rs2=0;
    int64_t imm=0;
    uint32_t bits=0U,pred=15U,succ=15U;
    char base[128];
    *handled = true;

    if (strcmp(op, ".insn") == 0) {
        MiniAsMCInst *m;
        uint32_t word;
        if (!parse_raw_insn_directive(stmt->args, &word)) goto invalid;
        m = calloc(1U, sizeof(*m));
        if (m == NULL) goto oom;
        m->opcode = RV_MC_RAW;
        m->encoding = word;
        stmt->mcinst = m;
        return true;
    }
    if (strcmp(op,"c.li")==0) {
        MiniAsMCInst *m;
        if(n!=2U || (rd=reg_number(ops[0]))<=0 ||
           !parse_i64(ops[1],&imm) || imm < -32 || imm > 31)
            goto invalid;
        m=calloc(1U,sizeof(*m)+2U*sizeof(*m->operands));
        if(m==NULL)goto oom;
        m->opcode=RV_MC_C16;
        m->encoding=(2U<<13U) |
                    (((uint32_t)imm>>5U&1U)<<12U) |
                    ((uint32_t)rd<<7U) |
                    (((uint32_t)imm&31U)<<2U) | 1U;
        m->count=2U;
        rv_set_reg(&m->operands[0],rd);
        rv_set_imm(&m->operands[1],imm);
        stmt->mcinst=m;
        return true;
    }
    if(strcmp(op,"nop")==0 || strcmp(op,"ret")==0 ||
       strcmp(op,"sret")==0 || strcmp(op,"ecall")==0 ||
       strcmp(op,"ebreak")==0 || strcmp(op,"wfi")==0 ||
       strcmp(op,"pause")==0 || strcmp(op,"fence.i")==0) {
        if(n!=0U)goto invalid;
        bits=strcmp(op,"nop")==0?0x00000013U:
             strcmp(op,"ret")==0?0x00008067U:
             strcmp(op,"sret")==0?0x10200073U:
             strcmp(op,"ecall")==0?0x00000073U:
             strcmp(op,"ebreak")==0?0x00100073U:
             strcmp(op,"wfi")==0?0x10500073U:
             strcmp(op,"pause")==0?0x0100000fU:0x0000100fU;
        return rv_mc_word(as,stmt,bits,0,0,0);
    }
    if(strcmp(op,"fence")==0) {
        if(n==2U) {
            if(!parse_fence_set(ops[0],&pred)||
               !parse_fence_set(ops[1],&succ))goto invalid;
        }else if(n!=0U)goto invalid;
        return rv_mc_word(as,stmt,0x0fU|((pred<<4U|succ)<<20U),0,0,0);
    }
    if(strcmp(op,"sfence.vma")==0) {
        if(n>2U ||
           (n>=1U && (rs1=reg_number(ops[0]))<0) ||
           (n==2U && (rs2=reg_number(ops[1]))<0))goto invalid;
        return rv_mc_word(as,stmt,0x12000073U,0,rs1,rs2);
    }
    if(strcmp(op,"jalr")==0) {
        if(n==1U){rd=1;rs1=reg_number(ops[0]);}
        else if(n==2U) {
            rd=reg_number(ops[0]);
            if(!parse_mem(ops[1],&imm,base))goto invalid;
            rs1=reg_number(base);
        }else if(n==3U) {
            rd=reg_number(ops[0]);rs1=reg_number(ops[1]);
            if(!parse_i64(ops[2],&imm))goto invalid;
        }else goto invalid;
        if(rd<0||rs1<0||imm< -2048||imm>2047)goto invalid;
        return rv_mc_word(as,stmt,0x67U|(((uint32_t)imm&4095U)<<20U),
                          rd,rs1,0);
    }
    if(strcmp(op,"mv")==0 || strcmp(op,"move")==0 ||
       strcmp(op,"snez")==0 || strcmp(op,"seqz")==0 ||
       strcmp(op,"neg")==0 || strcmp(op,"negw")==0 ||
       strcmp(op,"not")==0) {
        if(n!=2U || (rd=reg_number(ops[0]))<0 ||
           (rs1=reg_number(ops[1]))<0)goto invalid;
        if(strcmp(op,"snez")==0)
            return rv_mc_word(as,stmt,0x00003033U,rd,0,rs1);
        if(strcmp(op,"neg")==0)
            return rv_mc_word(as,stmt,0x40000033U,rd,0,rs1);
        if(strcmp(op,"negw")==0)
            return rv_mc_word(as,stmt,0x4000003bU,rd,0,rs1);
        bits=strcmp(op,"seqz")==0?0x00103013U:
             strcmp(op,"not")==0?0xfff04013U:0x00000013U;
        return rv_mc_word(as,stmt,bits,rd,rs1,0);
    }
    if(strcmp(op,"jr")==0 || strcmp(op,"fscsr")==0 ||
       strcmp(op,"frcsr")==0) {
        if(n!=1U || (rs1=reg_number(ops[0]))<0)goto invalid;
        if(strcmp(op,"frcsr")==0)
            return rv_mc_word(as,stmt,0x00302073U,rs1,0,0);
        bits=strcmp(op,"fscsr")==0?0x00301073U:0x00000067U;
        return rv_mc_word(as,stmt,bits,0,rs1,0);
    }
    *handled=false;
    return true;
invalid:
    minias_set_error(as,"bad-riscv-mcinst:%s:%s:line=%zu",
                     op,stmt->args,stmt->line);
    return false;
oom:
    minias_set_error(as,"out-of-memory:riscv-compressed-mcinst");
    return false;
}


/* GNU multiword pseudos retain one layout-sized fragment, but their
 * registers, bit-patterns and symbolic expressions are parsed only once.
 * The serializer must never re-split their original source strings. */
static bool rv_parse_multi_mc(MiniAs *as, MiniAsStmt *stmt,
                              char ops[][128], size_t n, bool *handled) {
    const char *op = stmt->op;
    MiniAsMCInst *m;
    MiniAsSymbolExpr expr;
    uint64_t bits = 0U;
    bool fits = false;
    int64_t signed_value = 0;
    int rd = -1;
    unsigned family, slots;
    *handled = true;
    if (strcmp(op, "li") == 0) {
        if (n != 2U || (rd = reg_number(ops[0])) < 0 ||
            !parse_li_bits(ops[1], &bits, &fits, &signed_value))
            goto invalid;
        family = RV_MC_LI;
        slots = 3U;
    } else if (strcmp(op, "call") == 0 || strcmp(op, "tail") == 0) {
        if (n != 1U || !parse_call_target(ops[0], &expr)) goto invalid;
        family = strcmp(op, "call") == 0 ? RV_MC_CALL : RV_MC_TAIL;
        slots = 1U;
    } else if (strcmp(op, "lla") == 0 || strcmp(op, "la") == 0) {
        if (n != 2U || (rd = reg_number(ops[0])) < 0 ||
            !minias_parse_symbol_addend(ops[1], &expr)) goto invalid;
        family = RV_MC_LA;
        slots = 2U;
    } else {
        *handled = false;
        return true;
    }
    m = calloc(1U, sizeof(*m) + slots * sizeof(*m->operands));
    if (m == NULL) {
        minias_set_error(as, "out-of-memory:riscv-multi-mcinst");
        return false;
    }
    m->opcode = family;
    m->count = slots;
    if (family == RV_MC_LI) {
        m->encoding = fits ? 1U : 0U;
        rv_set_reg(&m->operands[0], rd);
        rv_set_imm(&m->operands[1], (int64_t)bits);
        rv_set_imm(&m->operands[2], signed_value);
    } else {
        unsigned symbol_index = family == RV_MC_LA ? 1U : 0U;
        if (family == RV_MC_LA) {
            rv_set_reg(&m->operands[0], rd);
            m->encoding = strcmp(op, "la") == 0 ? 1U : 0U;
        }
        m->operands[symbol_index].symbol = malloc(sizeof(expr));
        if (m->operands[symbol_index].symbol == NULL) {
            free(m);
            minias_set_error(as, "out-of-memory:riscv-multi-symbol");
            return false;
        }
        *m->operands[symbol_index].symbol = expr;
        m->operands[symbol_index].kind = MINIAS_MC_SYMBOL;
    }
    stmt->mcinst = m;
    return true;
invalid:
    minias_set_error(as, "bad-riscv-pseudo-mcinst:%s:%s:line=%zu",
                     op, stmt->args, stmt->line);
    return false;
}

static bool rv_parse_vector_mc(MiniAs *as, MiniAsStmt *stmt,
                               char operands[][128], size_t count,
                               bool *handled) {
    bool store;
    uint32_t width;
    int vd, rs1, rs2;
    char base[128];
    int64_t off;
    *handled = true;
    if (decode_vector_unit_stride(stmt->op, &store, &width)) {
        if ((count != 2U && count != 3U) ||
            (count == 3U && strcmp(operands[2], "v0.t") != 0) ||
            (vd = vector_reg_number(operands[0])) < 0 ||
            !parse_mem(operands[1], &off, base) || off != 0 ||
            (rs1 = reg_number(base)) < 0) goto invalid;
        return rv_mc_vector_word(as, stmt, ((count == 2U ? 1U : 0U) << 25U) |
                                 (width << 12U) |
                                 (store ? 0x27U : 0x07U),
                                 vd, rs1);
    }
    if (strcmp(stmt->op, "vmv.v.i") == 0) {
        if (count != 2U ||
            (vd = vector_reg_number(operands[0])) < 0 ||
            !parse_i64(operands[1], &off) || off < -16 || off > 15)
            goto invalid;
        return rv_mc_vector_word(as, stmt, (0x17U << 26U) | (1U << 25U) |
                                 (((uint32_t)off & 31U) << 15U) |
                                 (3U << 12U) | 0x57U, vd, 0);
    }
    if (strcmp(stmt->op, "vsetvli") == 0) {
        uint32_t vtype;
        if ((count != 5U && count != 6U) ||
            (vd = reg_number(operands[0])) < 0 ||
            (rs1 = reg_number(operands[1])) < 0 ||
            !parse_vsetvli_vtype(operands, count, &vtype)) goto invalid;
        return rv_mc_word(as, stmt,
                          (vtype << 20U) | (7U << 12U) | 0x57U,
                          vd, rs1, 0);
    }
    if (strcmp(stmt->op, "vsetvl") == 0) {
        if (count != 3U || (vd = reg_number(operands[0])) < 0 ||
            (rs1 = reg_number(operands[1])) < 0 ||
            (rs2 = reg_number(operands[2])) < 0) goto invalid;
        return rv_mc_word(as, stmt, (0x40U << 25U) | (7U << 12U) | 0x57U,
                          vd, rs1, rs2);
    }
    *handled = false;
    return true;
invalid:
    minias_set_error(as, "bad-vector-mcinst:%s:%s:line=%zu",
                     stmt->op, stmt->args, stmt->line);
    return false;
}

static bool rv_parse_atomic_mc(MiniAs *as, MiniAsStmt *stmt,
                               char operands[][128], size_t count,
                               bool *handled) {
    bool lr = false, recognized;
    uint32_t width, ordering, funct5;
    int rd, rs1, rs2;
    char base[128];
    int64_t off;
    recognized = decode_lr_sc_mnemonic(stmt->op, &lr, &width, &ordering);
    if (recognized) funct5 = lr ? 2U : 3U;
    else recognized = decode_amo_mnemonic(stmt->op,
                                          &funct5, &width, &ordering);
    *handled = recognized;
    if (!recognized) return true;
    if ((rd = count != 0U ? reg_number(operands[0]) : -1) < 0)
        goto invalid;
    if (lr) {
        if (count != 2U || !parse_mem(operands[1], &off, base) ||
            off != 0 || (rs1 = reg_number(base)) < 0) goto invalid;
        rs2 = 0;
    } else {
        if (count != 3U || (rs2 = reg_number(operands[1])) < 0 ||
            !parse_mem(operands[2], &off, base) ||
            off != 0 || (rs1 = reg_number(base)) < 0) goto invalid;
    }
    return rv_mc_word(as, stmt, 0x2fU | (width << 12U) | ordering |
                      (funct5 << 27U), rd, rs1, rs2);
invalid:
    minias_set_error(as, "bad-atomic-mcinst:%s:%s:line=%zu",
                     stmt->op, stmt->args, stmt->line);
    return false;
}

static bool rv_parse_csr_mc(MiniAs *as, MiniAsStmt *stmt,
                            char operands[][128], size_t count,
                            bool *handled) {
    const char *name = stmt->op;
    uint32_t csr, funct3;
    int rd = 0, rs1 = 0;
    int64_t zimm;
    const char *source;
    bool csr_read = strcmp(name, "csrr") == 0;
    bool csr_full = strcmp(name, "csrrw") == 0 ||
                    strcmp(name, "csrrc") == 0;
    bool csr_alias = strcmp(name, "csrw") == 0 ||
                     strcmp(name, "csrs") == 0 ||
                     strcmp(name, "csrc") == 0;
    *handled = csr_read || csr_full || csr_alias;
    if (!*handled) return true;

    if (csr_read) {
        if (count != 2U || (rd = reg_number(operands[0])) < 0 ||
            !parse_csr(operands[1], &csr)) goto invalid;
        funct3 = 2U;
        return rv_mc_word(as, stmt, 0x73U | (funct3 << 12U) |
                          (csr << 20U), rd, 0, 0);
    }
    if (csr_full) {
        if (count != 3U || (rd = reg_number(operands[0])) < 0 ||
            !parse_csr(operands[1], &csr)) goto invalid;
        source = operands[2];
        funct3 = strcmp(name, "csrrw") == 0 ? 1U : 3U;
    } else {
        if (count != 2U || !parse_csr(operands[0], &csr)) goto invalid;
        source = operands[1];
        funct3 = strcmp(name, "csrw") == 0 ? 1U :
                 strcmp(name, "csrs") == 0 ? 2U : 3U;
    }
    if (parse_i64(source, &zimm)) {
        if (zimm < 0 || zimm > 31) goto invalid;
        funct3 |= 4U;
        rs1 = (int)zimm;
    } else {
        rs1 = reg_number(source);
        if (rs1 < 0) goto invalid;
    }
    return rv_mc_word(as, stmt, 0x73U | (funct3 << 12U) |
                      (csr << 20U), rd, rs1, 0);
invalid:
    minias_set_error(as, "bad-csr-mcinst:%s:%s:line=%zu",
                     stmt->op, stmt->args, stmt->line);
    return false;
}


static bool rv_mc_fp_word(MiniAs *as, MiniAsStmt *stmt,
                          uint32_t base, int rd, int rs1, int rs2,
                          unsigned fpr_mask) {
    unsigned i;
    if (!rv_mc_word(as, stmt, base, rd, rs1, rs2)) return false;
    for (i = 0U; i < 3U; ++i)
        if ((fpr_mask & (1U << i)) != 0U)
            stmt->mcinst->operands[i].reg_class = MINIAS_MC_REG_FPR;
    return true;
}

static bool rv_parse_fp_mc(MiniAs *as, MiniAsStmt *stmt,
                            char operands[][128], size_t count,
                            bool *handled) {
    const char *op = stmt->op;
    uint32_t base = 0U, rm = 7U;
    int rd = -1, rs1 = -1, rs2 = 0;
    unsigned classes = 7U;
    char memory_base[128];
    int64_t offset;
    bool store;
    *handled = false;

    if (is_fp_binary_single(op) || is_fp_binary_double(op)) {
        *handled = true;
        if (count != 3U && count != 4U) goto invalid;
        rd = float_reg_number(operands[0]);
        rs1 = float_reg_number(operands[1]);
        rs2 = float_reg_number(operands[2]);
        if (count == 4U && !parse_fp_rounding_mode(operands[3], &rm))
            goto invalid;
        if (strcmp(op, "fadd.s") == 0) base = 0x00000053U;
        else if (strcmp(op, "fsub.s") == 0) base = 0x08000053U;
        else if (strcmp(op, "fmul.s") == 0) base = 0x10000053U;
        else if (strcmp(op, "fdiv.s") == 0) base = 0x18000053U;
        else if (strcmp(op, "fadd.d") == 0) base = 0x02000053U;
        else if (strcmp(op, "fsub.d") == 0) base = 0x0a000053U;
        else if (strcmp(op, "fmul.d") == 0) base = 0x12000053U;
        else base = 0x1a000053U;
        base |= rm << 12U;
    } else if (strcmp(op, "fsgnjn.d") == 0) {
        *handled = true;
        if (count != 3U) goto invalid;
        rd = float_reg_number(operands[0]);
        rs1 = float_reg_number(operands[1]);
        rs2 = float_reg_number(operands[2]);
        base = 0x22001053U;
    } else if (is_fp_compare_double(op)) {
        *handled = true;
        if (count != 3U) goto invalid;
        rd = reg_number(operands[0]);
        rs1 = float_reg_number(operands[1]);
        rs2 = float_reg_number(operands[2]);
        classes = 6U; /* integer result, two FP sources */
        base = strcmp(op, "feq.d") == 0 ? 0xa2002053U :
               strcmp(op, "flt.d") == 0 ? 0xa2001053U : 0xa2000053U;
    } else if (strcmp(op, "fmv.x.d") == 0 ||
               strcmp(op, "fmv.x.w") == 0) {
        *handled = true;
        if (count != 2U) goto invalid;
        rd = reg_number(operands[0]);
        rs1 = float_reg_number(operands[1]);
        classes = 2U;
        base = strcmp(op, "fmv.x.d") == 0 ? 0xe2000053U : 0xe0000053U;
    } else if (strcmp(op, "fmv.d.x") == 0 ||
               strcmp(op, "fmv.w.x") == 0) {
        *handled = true;
        if (count != 2U) goto invalid;
        rd = float_reg_number(operands[0]);
        rs1 = reg_number(operands[1]);
        classes = 1U;
        base = strcmp(op, "fmv.d.x") == 0 ? 0xf2000053U : 0xf0000053U;
    } else if (is_fp_convert(op)) {
        *handled = true;
        if (count != 2U && count != 3U) goto invalid;
        if (strcmp(op, "fcvt.d.w") == 0) {base=0xd2000053U;rm=0U;}
        else if (strcmp(op, "fcvt.d.wu") == 0) {base=0xd2100053U;rm=0U;}
        else if (strcmp(op, "fcvt.d.l") == 0) {base=0xd2200053U;rm=7U;}
        else if (strcmp(op, "fcvt.d.lu") == 0) {base=0xd2300053U;rm=7U;}
        else if (strcmp(op, "fcvt.w.d") == 0) {base=0xc2000053U;rm=7U;}
        else if (strcmp(op, "fcvt.wu.d") == 0) {base=0xc2100053U;rm=7U;}
        else if (strcmp(op, "fcvt.l.d") == 0) {base=0xc2200053U;rm=7U;}
        else if (strcmp(op, "fcvt.lu.d") == 0) {base=0xc2300053U;rm=7U;}
        else if (strcmp(op, "fcvt.d.s") == 0) {base=0x42000053U;rm=0U;}
        else {base=0x40100053U;rm=7U;}
        if (count == 3U && !parse_fp_rounding_mode(operands[2], &rm))
            goto invalid;
        if (fp_convert_int_to_double(op)) {
            rd=float_reg_number(operands[0]);
            rs1=reg_number(operands[1]);
            classes=1U;
        } else if (fp_convert_double_to_int(op)) {
            rd=reg_number(operands[0]);
            rs1=float_reg_number(operands[1]);
            classes=2U;
        } else {
            rd=float_reg_number(operands[0]);
            rs1=float_reg_number(operands[1]);
            classes=3U;
        }
        base |= rm << 12U;
    } else if (strcmp(op, "fld") == 0 || strcmp(op, "flw") == 0 ||
               strcmp(op, "fsd") == 0 || strcmp(op, "fsw") == 0) {
        MiniAsMCInst *m;
        *handled=true;
        store = strcmp(op, "fsd") == 0 || strcmp(op, "fsw") == 0;
        if (count != 2U || (rd = float_reg_number(operands[0])) < 0 ||
            !parse_mem(operands[1], &offset, memory_base) ||
            (rs1 = reg_number(memory_base)) < 0 ||
            offset < -2048 || offset > 2047) goto invalid;
        m=calloc(1U,sizeof(*m)+3U*sizeof(*m->operands));
        if (m==NULL) {
            minias_set_error(as,"out-of-memory:riscv-fp-memory");
            return false;
        }
        m->opcode=store ? RV_MC_STORE : RV_MC_LOAD;
        m->encoding=(store ? 0x27U : 0x07U) |
                    ((strcmp(op,"fld")==0 || strcmp(op,"fsd")==0
                     ? 3U : 2U) << 12U);
        m->count=3U;
        rv_set_reg(&m->operands[0],rd);
        m->operands[0].reg_class=MINIAS_MC_REG_FPR;
        rv_set_reg(&m->operands[1],rs1);
        rv_set_imm(&m->operands[2],offset);
        stmt->mcinst=m;
        return true;
    } else return true;

    if (rd < 0 || rs1 < 0 || rs2 < 0) goto invalid;
    return rv_mc_fp_word(as,stmt,base,rd,rs1,rs2,classes);
invalid:
    minias_set_error(as,"bad-fp-mcinst:%s:%s:line=%zu",
                     op,stmt->args,stmt->line);
    return false;
}
bool minias_riscv_parse_mcinst(MiniAs *as, MiniAsStmt *stmt) {
    const RvMCDesc *desc = NULL;
    char operands[8][128];
    size_t count, i;
    int rd = -1, rs1 = -1, rs2 = -1;
    int64_t imm = 0;
    bool is_imm = false;
    MiniAsMCInst *inst;
    unsigned family, slots;
    uint32_t encoding;
    char base[128];
    const char *op = stmt->op;

    for (i = 0U; i < sizeof(rv_mc_table) / sizeof(rv_mc_table[0]); ++i) {
        if (strcmp(op, rv_mc_table[i].name) == 0) {
            desc = &rv_mc_table[i];
            break;
        }
    }
    if (desc == NULL) {
        bool handled;
        count = split_operands(stmt->args, operands, 8U);
        if (!rv_parse_simple_mc(as, stmt, operands, count, &handled) ||
            handled) return as->error[0] == '\0';
        if (!rv_parse_multi_mc(as, stmt, operands, count, &handled) ||
            handled) return as->error[0] == '\0';
        if (!rv_parse_vector_mc(as, stmt, operands, count, &handled) ||
            handled) return as->error[0] == '\0';
        if (!rv_parse_atomic_mc(as, stmt, operands, count, &handled) ||
            handled) return as->error[0] == '\0';
        if (!rv_parse_csr_mc(as, stmt, operands, count, &handled) ||
            handled) return as->error[0] == '\0';
        if (!rv_parse_fp_mc(as, stmt, operands, count, &handled) ||
            handled) return as->error[0] == '\0';
        return true;
    }
    family = desc->family;
    encoding = desc->base;
    count = split_operands(stmt->args, operands, 8U);

    if (family == RV_MC_R || family == RV_MC_I) {
        bool two = strcmp(op,"sext.w") == 0 ||
                   strcmp(op,"orc.b") == 0 ||
                   strcmp(op,"rev8") == 0 || strcmp(op,"ctz") == 0;
        if (count != (two ? 2U : 3U)) goto invalid;
        if (two) {
            rd = reg_number(operands[0]);
            rs1 = reg_number(operands[1]);
            imm = strcmp(op,"orc.b") == 0 ? 0x287 :
                  strcmp(op,"rev8") == 0 ? 0x6b8 :
                  strcmp(op,"ctz") == 0 ? 0x601 : 0;
            family = RV_MC_I;
        } else if (count == 3U) {
            rd = reg_number(operands[0]);
            rs1 = reg_number(operands[1]);
            if (family == RV_MC_R) {
                rs2 = reg_number(operands[2]);
                if (rs2 < 0 &&
                    (strcmp(op, "add") == 0 || strcmp(op, "sub") == 0 ||
                     strcmp(op, "and") == 0 || strcmp(op, "or") == 0 ||
                     strcmp(op, "xor") == 0 || strcmp(op, "sll") == 0 ||
                     strcmp(op, "srl") == 0 || strcmp(op, "sra") == 0) &&
                    parse_i64(operands[2], &imm)) {
                    family = RV_MC_I;
                    is_imm = true;
                    encoding = 0x13U | (encoding & 0x7000U);
                    if (strcmp(op,"sub") == 0) {
                        if (imm == INT64_MIN) goto invalid;
                        imm = -imm;
                    } else if (strcmp(op,"sra") == 0) {
                        encoding |= 0x40000000U;
                    }
                }
            } else {
                is_imm = parse_i64(operands[2], &imm);
            }
        }
        if (rd < 0 || rs1 < 0 || (family == RV_MC_R && rs2 < 0) ||
            (family == RV_MC_I && !is_imm && count != 2U &&
             !parse_i64(operands[2], &imm))) goto invalid;
        if (family == RV_MC_I) {
            bool shift = ((encoding & 0x7000U) == 0x1000U ||
                          (encoding & 0x7000U) == 0x5000U) &&
                         (strcmp(op,"slli")==0 || strcmp(op,"srli")==0 ||
                          strcmp(op,"srai")==0 || strcmp(op,"slliw")==0 ||
                          strcmp(op,"srliw")==0 || strcmp(op,"sraiw")==0 ||
                          strcmp(op,"sll")==0 || strcmp(op,"srl")==0 ||
                          strcmp(op,"sra")==0);
            if (shift) {
                int max = ((encoding & 0x7fU) == 0x1bU ||
                           as->target_xlen == 32U) ? 31 : 63;
                if (imm < 0 || imm > max) goto invalid;
            } else if ((strcmp(op, "orc.b") != 0) &&
                       (strcmp(op, "rev8") != 0) &&
                       (strcmp(op, "ctz") != 0) &&
                       (imm < -2048 || imm > 2047)) goto invalid;
        }
        if (strcmp(op, "sgt")==0 || strcmp(op,"sgtu")==0) {
            int tmp = rs1; rs1 = rs2; rs2 = tmp;
        }
        slots = 3U;
    } else if (family == RV_MC_LOAD || family == RV_MC_STORE) {
        bool symbolic = false;
        MiniAsSymbolExpr expr;
        rd = count != 0U ? reg_number(operands[0]) : -1;
        if (rd < 0) goto invalid;
        if (count == 2U && parse_mem(operands[1], &imm, base)) {
            rs1 = reg_number(base);
            if (rs1 < 0 || imm < -2048 || imm > 2047) goto invalid;
        } else if (family == RV_MC_LOAD && count == 2U &&
                   minias_parse_symbol_addend(operands[1], &expr)) {
            /* GNU symbolic load: AUIPC rd + LOAD rd,0(rd). */
            rs1 = rd;
            symbolic = true;
        } else if (family == RV_MC_STORE && count == 3U &&
                   minias_parse_symbol_addend(operands[1], &expr)) {
            /* GNU symbolic store: AUIPC temp + STORE data,0(temp). */
            rs1 = reg_number(operands[2]);
            if (rs1 < 0) goto invalid;
            symbolic = true;
        } else goto invalid;
        slots = 3U;
        if (symbolic) {
            inst = calloc(1U, sizeof(*inst) + slots * sizeof(*inst->operands));
            if (inst == NULL) goto oom;
            inst->opcode = family == RV_MC_LOAD ?
                           RV_MC_SYMBOL_LOAD : RV_MC_SYMBOL_STORE;
            inst->encoding = encoding;
            inst->count = slots;
            rv_set_reg(&inst->operands[0], rd);
            rv_set_reg(&inst->operands[1], rs1);
            inst->operands[2].symbol = malloc(sizeof(expr));
            if (inst->operands[2].symbol == NULL) {
                free(inst);
                goto oom;
            }
            *inst->operands[2].symbol = expr;
            inst->operands[2].kind = MINIAS_MC_SYMBOL;
            stmt->mcinst = inst;
            return true;
        }
    } else if (family == RV_MC_U) {
        if (count != 2U) goto invalid;
        rd = reg_number(operands[0]);
        if (rd < 0 || !parse_i64(operands[1], &imm)) goto invalid;
        slots = 2U;
    } else if (family == RV_MC_BRANCH) {
        bool two = strcmp(op, "beqz") == 0 || strcmp(op, "bnez") == 0 ||
                   strcmp(op, "bltz") == 0 || strcmp(op, "bgez") == 0 ||
                   strcmp(op, "bgtz") == 0 || strcmp(op, "blez") == 0;
        bool reverse = strcmp(op, "bgt") == 0 || strcmp(op, "ble") == 0 ||
                       strcmp(op, "bgtu") == 0 || strcmp(op, "bleu") == 0;
        const char *symbol_text;
        if (count != (two ? 2U : 3U)) goto invalid;
        rs1 = reg_number(operands[0]);
        if (rs1 < 0) goto invalid;
        if (two) {
            rs2 = 0;
            symbol_text = operands[1];
            if (strcmp(op,"bgtz") == 0 || strcmp(op,"blez") == 0) {
                rs2 = rs1; rs1 = 0;
            }
        } else {
            rs2 = reg_number(operands[1]);
            if (rs2 < 0) goto invalid;
            if (reverse) {int tmp=rs1;rs1=rs2;rs2=tmp;}
            symbol_text = operands[2];
        }
        {
            MiniAsSymbolExpr parsed;
            if (!minias_parse_symbol_addend(symbol_text,&parsed)) goto invalid;
            slots=3U;
            inst=calloc(1U,sizeof(*inst)+slots*sizeof(*inst->operands));
            if (inst==NULL) goto oom;
            inst->opcode=family; inst->encoding=encoding; inst->count=slots;
            rv_set_reg(&inst->operands[0],rs1);
            rv_set_reg(&inst->operands[1],rs2);
            inst->operands[2].symbol=malloc(sizeof(parsed));
            if(inst->operands[2].symbol==NULL){free(inst);goto oom;}
            *inst->operands[2].symbol=parsed;
            inst->operands[2].kind=MINIAS_MC_SYMBOL;
            stmt->mcinst=inst;
        }
        return true;
    } else if (family == RV_MC_JAL) {
        const char *sym;
        rd = strcmp(op, "j") == 0 ? 0 : 1;
        if (strcmp(op, "j") == 0 && count != 1U) goto invalid;
        if (count == 1U) sym = operands[0];
        else if (count == 2U && strcmp(op,"jal")==0) {
            rd=reg_number(operands[0]);
            sym=operands[1];
        } else goto invalid;
        if (rd < 0) goto invalid;
        {
            MiniAsSymbolExpr parsed;
            if (!minias_parse_symbol_addend(sym,&parsed)) goto invalid;
            slots=2U;
            inst=calloc(1U,sizeof(*inst)+slots*sizeof(*inst->operands));
            if(inst==NULL) goto oom;
            inst->opcode=family; inst->encoding=encoding; inst->count=slots;
            rv_set_reg(&inst->operands[0],rd);
            inst->operands[1].symbol=malloc(sizeof(parsed));
            if(inst->operands[1].symbol==NULL){free(inst);goto oom;}
            *inst->operands[1].symbol=parsed;
            inst->operands[1].kind=MINIAS_MC_SYMBOL;
            stmt->mcinst=inst;
        }
        return true;
    } else return true;

    inst=calloc(1U,sizeof(*inst)+slots*sizeof(*inst->operands));
    if(inst==NULL)goto oom;
    inst->opcode=family;inst->encoding=encoding;inst->count=slots;
    rv_set_reg(&inst->operands[0],rd);
    if(family==RV_MC_R) {
        rv_set_reg(&inst->operands[1],rs1);
        rv_set_reg(&inst->operands[2],rs2);
    } else if(family==RV_MC_I) {
        rv_set_reg(&inst->operands[1],rs1);
        rv_set_imm(&inst->operands[2],imm);
    } else if(family==RV_MC_LOAD || family==RV_MC_STORE){
        rv_set_reg(&inst->operands[1],rs1);
        rv_set_imm(&inst->operands[2],imm);
    } else rv_set_imm(&inst->operands[1],imm);
    stmt->mcinst=inst;
    return true;
invalid:
    minias_set_error(as,"invalid-riscv-mcinst:%s:%s:line=%zu",
                     op,stmt->args,stmt->line);
    return false;
oom:
    minias_set_error(as,"out-of-memory:riscv-mcinst");
    return false;
}


/* Typed variable-size RISC-V fragments: exactly one symbol/number parse
 * happened at statement creation. Layout owns size; this routine writes
 * the same byte stream, relocations and PIC GOT selection as the proven
 * legacy pseudo expander, without reopening the instruction text. */
static bool rv_encode_multi_mc(MiniAs *as, const MiniAsStmt *stmt) {
    const MiniAsMCInst *m = stmt->mcinst;
    if (m->opcode == RV_MC_LI) {
        uint64_t bits = m->operands[1].immediate;
        int64_t signed_value = (int64_t)m->operands[2].immediate;
        bool fits_signed32 = m->encoding != 0U;
        int rd = (int)m->operands[0].reg;
        if (as->target_xlen == 32U) {
            int64_t rv32_value, hi, lo;
            if (!rv32_li_value(bits, &rv32_value)) {
                minias_set_error(as, "rv32i-li-overflows-32-bits:line=%zu",
                                 stmt->line);
                return false;
            }
            if (rv32_value >= -2048 && rv32_value <= 2047)
                return append_u32(as, stmt->section,
                                  enc_i(0x13U, rd, 0U, 0, rv32_value));
            hi = (rv32_value + INT64_C(2048)) >> 12;
            lo = rv32_value - hi * INT64_C(4096);
            if (!append_u32(as, stmt->section,
                            0x37U | ((uint32_t)rd << 7U) |
                            (((uint32_t)hi & UINT32_C(0xfffff)) << 12U)))
                return false;
            return lo == 0 || append_u32(as, stmt->section,
                                         enc_i(0x13U, rd, 0U, rd, lo));
        }
        if (fits_signed32) {
            int64_t hi, lo;
            if (signed_value >= -2048 && signed_value <= 2047)
                return append_u32(as, stmt->section,
                                  enc_i(0x13U, rd, 0U, 0, signed_value));
            hi = (signed_value + INT64_C(2048)) >> 12;
            lo = signed_value - hi * INT64_C(4096);
            if (!append_u32(as, stmt->section,
                            0x37U | ((uint32_t)rd << 7U) |
                            (((uint32_t)hi & UINT32_C(0xfffff)) << 12U)))
                return false;
            return append_u32(as, stmt->section,
                              enc_i(0x1bU, rd, 0U, rd, lo));
        }
        {
            int index = 7, i;
            uint64_t top;
            while (index > 0 &&
                   ((bits >> ((unsigned)index * 8U)) & UINT64_C(0xff)) == 0U)
                --index;
            top = (bits >> ((unsigned)index * 8U)) & UINT64_C(0xff);
            if (!append_u32(as, stmt->section,
                            enc_i(0x13U, rd, 0U, 0, (int64_t)top)))
                return false;
            for (i = index - 1; i >= 0; --i) {
                uint64_t byte = (bits >> ((unsigned)i * 8U)) & UINT64_C(0xff);
                if (!append_u32(as, stmt->section,
                                enc_i(0x13U, rd, 1U, rd, 8)))
                    return false;
                if (byte != 0U &&
                    !append_u32(as, stmt->section,
                                enc_i(0x13U, rd, 0U, rd, (int64_t)byte)))
                    return false;
            }
            return true;
        }
    }

    if (m->opcode == RV_MC_CALL || m->opcode == RV_MC_TAIL) {
        const MiniAsSymbolExpr *expr = m->operands[0].symbol;
        if (expr == NULL) {
            minias_set_error(as, "internal:riscv-call-symbol-missing");
            return false;
        }
        if (m->opcode == RV_MC_CALL) {
            if (!append_u32(as, stmt->section, 0x00000097U) ||
                !append_u32(as, stmt->section,
                            enc_i(0x67U, 1, 0U, 1, 0))) return false;
        } else {
            if (!append_u32(as, stmt->section, 0x00000317U) ||
                !append_u32(as, stmt->section,
                            enc_i(0x67U, 0, 0U, 6, 0))) return false;
        }
        return minias_add_fixup(as, stmt->section, stmt->offset,
                                MINIAS_FIXUP_RV_CALL_PLT, 4U,
                                expr->name, expr->addend);
    }

    if (m->opcode == RV_MC_LA) {
        const MiniAsSymbolExpr *expr = m->operands[1].symbol;
        MiniAsSymbol *anchor, *target;
        char name[96];
        int rd = (int)m->operands[0].reg;
        bool use_got;
        if (expr == NULL) {
            minias_set_error(as, "internal:riscv-la-symbol-missing");
            return false;
        }
        target = minias_get_symbol(as, expr->name, false);
        /* Linux early boot: NOPIC is default; PIC external la may use a
         * GOT reference only if the source did not request an addend. */
        use_got = (stmt->site.mode_bits & MINIAS_RISCV_OPTION_PIC) != 0U &&
                  m->encoding != 0U && expr->addend == 0 &&
                  (target == NULL || !target->defined);
        (void)snprintf(name, sizeof(name), ".Lminias_pcrel_%zu",
                       ++as->pcrel_anchor_counter);
        anchor = minias_get_symbol(as, name, true);
        if (anchor == NULL) return false;
        anchor->defined = true;
        anchor->section = stmt->section;
        anchor->subsection = stmt->subsection;
        anchor->value = stmt->offset;
        anchor->bind = MINIAS_STB_LOCAL;

        if (!append_u32(as, stmt->section,
                        0x17U | ((uint32_t)rd << 7U))) return false;
        if (use_got) {
            if (!append_u32(as, stmt->section,
                            enc_i(0x03U, rd, 3U, rd, 0))) return false;
        } else if (!append_u32(as, stmt->section,
                               enc_i(0x13U, rd, 0U, rd, 0))) return false;
        return minias_add_fixup(as, stmt->section, stmt->offset,
                                use_got ? MINIAS_FIXUP_RV_GOT_HI20 :
                                          MINIAS_FIXUP_RV_PCREL_HI20,
                                4U, expr->name, expr->addend) &&
               minias_add_fixup(as, stmt->section, stmt->offset + 4U,
                                MINIAS_FIXUP_RV_PCREL_LO12_I, 4U, name, 0);
    }

    minias_set_error(as, "internal:unknown-riscv-multi-mcinst:%u",
                     m->opcode);
    return false;
}

static bool rv_encode_mcinst(MiniAs *as, const MiniAsStmt *stmt) {
    const MiniAsMCInst *m=stmt->mcinst;
    if (m->opcode == RV_MC_RAW)
        return append_u32(as, stmt->section, m->encoding);
    if (m->opcode == RV_MC_LI || m->opcode == RV_MC_CALL ||
        m->opcode == RV_MC_TAIL || m->opcode == RV_MC_LA)
        return rv_encode_multi_mc(as, stmt);
    int rd=(int)m->operands[0].reg,rs1=0,rs2=0;
    int64_t imm=0,distance;
    uint32_t base=m->encoding,code=0U;
    MiniAsSymbolExpr const *expr=NULL;
    MiniAsSymbol const *symbol=NULL;
    if (m->count > 1U && m->operands[1].kind==MINIAS_MC_REGISTER)
        rs1=(int)m->operands[1].reg;
    if (m->count > 2U && m->operands[2].kind==MINIAS_MC_REGISTER)
        rs2=(int)m->operands[2].reg;
    if (m->count > 2U && m->operands[2].kind==MINIAS_MC_IMMEDIATE)
        imm=(int64_t)m->operands[2].immediate;
    if (m->opcode == RV_MC_C16)
        return append_u16(as,stmt->section,(uint16_t)base);
    if (m->opcode == RV_MC_SYMBOL_LOAD ||
        m->opcode == RV_MC_SYMBOL_STORE) {
        const bool load = m->opcode == RV_MC_SYMBOL_LOAD;
        const MiniAsSymbolExpr *ref = m->operands[2].symbol;
        char label[96];
        MiniAsSymbol *anchor;
        uint32_t second;
        if (ref == NULL) {
            minias_set_error(as, "internal:missing-riscv-symbol-fragment");
            return false;
        }
        (void)snprintf(label, sizeof(label),
                       load ? ".Lminias_load_%zu" : ".Lminias_store_%zu",
                       ++as->pcrel_anchor_counter);
        anchor = minias_get_symbol(as, label, true);
        if (anchor == NULL) return false;
        anchor->defined = true;
        anchor->section = stmt->section;
        anchor->subsection = stmt->subsection;
        anchor->value = stmt->offset;
        anchor->bind = MINIAS_STB_LOCAL;
        second = base |
                 ((uint32_t)rs1 << 15U) |
                 ((uint32_t)rd << (load ? 7U : 20U));
        if (!append_u32(as, stmt->section,
                        0x17U | ((uint32_t)rs1 << 7U)) ||
            !append_u32(as, stmt->section, second))
            return false;
        return minias_add_fixup(as, stmt->section, stmt->offset,
                                MINIAS_FIXUP_RV_PCREL_HI20, 4U,
                                ref->name, ref->addend) &&
               minias_add_fixup(as, stmt->section, stmt->offset + 4U,
                                load ? MINIAS_FIXUP_RV_PCREL_LO12_I :
                                       MINIAS_FIXUP_RV_PCREL_LO12_S,
                                4U, label, 0);
    }
    if (m->opcode == RV_MC_WORD)
        code = base | ((uint32_t)rd << 7U) |
               ((uint32_t)rs1 << 15U) | ((uint32_t)rs2 << 20U);
    else if (m->opcode==RV_MC_R)
        code=enc_r(base&0x7fU,rd,(base>>12U)&7U,rs1,rs2,base>>25U);
    else if(m->opcode==RV_MC_I || m->opcode==RV_MC_LOAD) {
        if(m->opcode==RV_MC_I && m->count==2U)
            imm=(int64_t)m->operands[1].immediate;
        code=base | ((uint32_t)rd<<7U) |
             ((uint32_t)rs1<<15U) |
             (((uint32_t)imm&0xfffU)<<20U);
    } else if(m->opcode==RV_MC_STORE) {
        uint32_t bits=(uint32_t)imm&0xfffU;
        code=base | ((uint32_t)rd<<20U) |
             ((uint32_t)rs1<<15U) |
             ((bits&31U)<<7U) | (((bits>>5U)&127U)<<25U);
    } else if(m->opcode==RV_MC_U)
        code=base | ((uint32_t)rd<<7U) |
             (((uint32_t)m->operands[1].immediate&0xfffffU)<<12U);
    else if(m->opcode==RV_MC_BRANCH || m->opcode==RV_MC_JAL) {
        expr=m->operands[m->opcode==RV_MC_BRANCH ? 2U : 1U].symbol;
        symbol=minias_get_symbol(as,expr->name,false);
        if(m->opcode==RV_MC_BRANCH &&
           (symbol==NULL || !symbol->defined ||
            symbol->section!=stmt->section)){
            minias_set_error(as,"unsupported-relocation:%s:line=%zu",
                             stmt->op,stmt->line);
            return false;
        }
        if(symbol!=NULL && symbol->defined &&
           symbol->section==stmt->section &&
           (m->opcode==RV_MC_BRANCH ||
            symbol->subsection==stmt->subsection)){
            if(symbol->value>INT64_MAX||stmt->offset>INT64_MAX){
                minias_set_error(as,"riscv-branch-offset-overflow");
                return false;
            }
            distance=(int64_t)symbol->value-(int64_t)stmt->offset;
            if((expr->addend>0 && distance>INT64_MAX-expr->addend)||
               (expr->addend<0 && distance<INT64_MIN-expr->addend)){
                minias_set_error(as,"riscv-branch-addend-overflow");
                return false;
            }
            distance+=expr->addend;
            if(m->opcode==RV_MC_JAL){
                if((distance&1)!=0||distance<-(1LL<<20)||
                   distance>(1LL<<20)-2){
                    minias_set_error(as,"jal-range:line=%zu",stmt->line);
                    return false;
                }
                return append_u32(as,stmt->section,enc_j(rd,distance));
            }
            if(stmt->size==8U){
                int64_t jdiff=distance-4;
                if((jdiff&1)!=0||jdiff<-(1LL<<20)||jdiff>(1LL<<20)-2){
                    minias_set_error(as,"branch-long-jal-range:%s:line=%zu",
                                     stmt->op,stmt->line);
                    return false;
                }
                return append_u32(as,stmt->section,
                           enc_b((base>>12U &7U)^1U,rd,rs1,8)) &&
                       append_u32(as,stmt->section,enc_j(0,jdiff));
            }
            if((distance&1)!=0||distance< -4096||distance>4094){
                minias_set_error(as,"branch-range:%s:line=%zu",
                                 stmt->op,stmt->line);
                return false;
            }
            return append_u32(as,stmt->section,
                              enc_b((base>>12U)&7U,rd,rs1,distance));
        }
        return append_u32(as,stmt->section,enc_j(rd,0)) &&
               minias_add_fixup(as,stmt->section,stmt->offset,
                                MINIAS_FIXUP_RV_JAL,4U,expr->name,expr->addend);
    } else {
        minias_set_error(as,"unknown-riscv-mcinst");
        return false;
    }
    return append_u32(as,stmt->section,code);
}

bool minias_riscv_encode(MiniAs *as, const MiniAsStmt *stmt) {
    if (stmt->mcinst == NULL) {
        minias_set_error(as, "internal:riscv-missing-typed-instruction:%s:line=%zu",
                         stmt->op, stmt->line);
        return false;
    }
    return rv_encode_mcinst(as, stmt);
}
