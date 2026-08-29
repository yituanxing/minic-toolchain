#include "minias_internal.h"

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
    char *end = NULL;
    long value;

    if (name == NULL || name[0] != 'f' || !isdigit((unsigned char)name[1])) {
        return -1;
    }
    errno = 0;
    value = strtol(name + 1, &end, 10);
    if (errno != 0 || end == name + 1 || *end != '\0' || value < 0 || value > 31) {
        return -1;
    }
    return (int)value;
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

typedef struct MiniAsConstExpressionParser {
    const char *cursor;
} MiniAsConstExpressionParser;

static void skip_const_space(MiniAsConstExpressionParser *parser) {
    while (*parser->cursor == ' ' || *parser->cursor == '\t') {
        ++parser->cursor;
    }
}

static bool parse_const_or(MiniAsConstExpressionParser *parser, uint64_t *value);

static bool parse_const_primary(MiniAsConstExpressionParser *parser,
                                uint64_t *value) {
    char *end = NULL;
    unsigned long long parsed;

    skip_const_space(parser);
    if (*parser->cursor == '(') {
        ++parser->cursor;
        if (!parse_const_or(parser, value)) {
            return false;
        }
        skip_const_space(parser);
        if (*parser->cursor != ')') {
            return false;
        }
        ++parser->cursor;
        return true;
    }

    errno = 0;
    parsed = strtoull(parser->cursor, &end, 0);
    if (errno != 0 || end == parser->cursor) {
        return false;
    }
    parser->cursor = end;
    *value = (uint64_t)parsed;
    return true;
}

static bool parse_const_unary(MiniAsConstExpressionParser *parser,
                              uint64_t *value) {
    skip_const_space(parser);
    if (*parser->cursor == '+') {
        ++parser->cursor;
        return parse_const_unary(parser, value);
    }
    if (*parser->cursor == '-') {
        uint64_t operand;
        ++parser->cursor;
        if (!parse_const_unary(parser, &operand)) {
            return false;
        }
        *value = UINT64_C(0) - operand;
        return true;
    }
    if (*parser->cursor == '~') {
        uint64_t operand;
        ++parser->cursor;
        if (!parse_const_unary(parser, &operand)) {
            return false;
        }
        *value = ~operand;
        return true;
    }
    return parse_const_primary(parser, value);
}

static bool parse_const_mul(MiniAsConstExpressionParser *parser,
                            uint64_t *value) {
    uint64_t result;

    if (!parse_const_unary(parser, &result)) {
        return false;
    }
    for (;;) {
        char op;
        uint64_t rhs;

        skip_const_space(parser);
        op = *parser->cursor;
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        ++parser->cursor;
        if (!parse_const_unary(parser, &rhs)) {
            return false;
        }
        if (op == '*') {
            result *= rhs;
        } else {
            if (rhs == 0U) {
                return false;
            }
            result = op == '/' ? result / rhs : result % rhs;
        }
    }
    *value = result;
    return true;
}

static bool parse_const_sum(MiniAsConstExpressionParser *parser,
                            uint64_t *value) {
    uint64_t result;

    if (!parse_const_mul(parser, &result)) {
        return false;
    }
    for (;;) {
        char op;
        uint64_t rhs;

        skip_const_space(parser);
        op = *parser->cursor;
        if (op != '+' && op != '-') {
            break;
        }
        ++parser->cursor;
        if (!parse_const_mul(parser, &rhs)) {
            return false;
        }
        result = op == '+' ? result + rhs : result - rhs;
    }
    *value = result;
    return true;
}

static bool parse_const_shift(MiniAsConstExpressionParser *parser,
                              uint64_t *value) {
    uint64_t result;

    if (!parse_const_sum(parser, &result)) {
        return false;
    }
    for (;;) {
        bool left;
        uint64_t rhs;

        skip_const_space(parser);
        if (strncmp(parser->cursor, "<<", 2U) == 0) {
            left = true;
        } else if (strncmp(parser->cursor, ">>", 2U) == 0) {
            left = false;
        } else {
            break;
        }
        parser->cursor += 2;
        if (!parse_const_sum(parser, &rhs) || rhs >= 64U) {
            return false;
        }
        result = left ? result << (unsigned int)rhs
                      : result >> (unsigned int)rhs;
    }
    *value = result;
    return true;
}

static bool parse_const_and(MiniAsConstExpressionParser *parser,
                            uint64_t *value) {
    uint64_t result;

    if (!parse_const_shift(parser, &result)) {
        return false;
    }
    for (;;) {
        uint64_t rhs;
        skip_const_space(parser);
        if (*parser->cursor != '&' || parser->cursor[1] == '&') {
            break;
        }
        ++parser->cursor;
        if (!parse_const_shift(parser, &rhs)) {
            return false;
        }
        result &= rhs;
    }
    *value = result;
    return true;
}

static bool parse_const_xor(MiniAsConstExpressionParser *parser,
                            uint64_t *value) {
    uint64_t result;

    if (!parse_const_and(parser, &result)) {
        return false;
    }
    for (;;) {
        uint64_t rhs;
        skip_const_space(parser);
        if (*parser->cursor != '^') {
            break;
        }
        ++parser->cursor;
        if (!parse_const_and(parser, &rhs)) {
            return false;
        }
        result ^= rhs;
    }
    *value = result;
    return true;
}

static bool parse_const_or(MiniAsConstExpressionParser *parser,
                           uint64_t *value) {
    uint64_t result;

    if (!parse_const_xor(parser, &result)) {
        return false;
    }
    for (;;) {
        uint64_t rhs;
        skip_const_space(parser);
        if (*parser->cursor != '|' || parser->cursor[1] == '|') {
            break;
        }
        ++parser->cursor;
        if (!parse_const_xor(parser, &rhs)) {
            return false;
        }
        result |= rhs;
    }
    *value = result;
    return true;
}

static bool parse_const_expression_bits(const char *text, uint64_t *value) {
    MiniAsConstExpressionParser parser;

    parser.cursor = text;
    if (!parse_const_or(&parser, value)) {
        return false;
    }
    skip_const_space(&parser);
    return *parser.cursor == '\0';
}

static bool parse_i64(const char *text, int64_t *value) {
    uint64_t bits;

    if (!parse_const_expression_bits(text, &bits)) {
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

    if (!parse_const_expression_bits(text, bits)) {
        return false;
    }
    *signed_value = (int64_t)*bits;
    *fits_signed32 =
        *signed_value >= INT32_MIN && *signed_value <= INT32_MAX;
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

static uint32_t enc_s(uint32_t funct3, int rs1, int rs2, int64_t imm) {
    uint32_t value = (uint32_t)imm & 0xfffU;

    return 0x23U | ((value & 0x1fU) << 7U) | (funct3 << 12U) |
           ((uint32_t)rs1 << 15U) | ((uint32_t)rs2 << 20U) |
           (((value >> 5U) & 0x7fU) << 25U);
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
    return minias_section_append(as, section, bytes, sizeof(bytes));
}

static bool append_u32(MiniAs *as, int section, uint32_t value) {
    unsigned char bytes[4];

    bytes[0] = (unsigned char)(value & 0xffU);
    bytes[1] = (unsigned char)((value >> 8U) & 0xffU);
    bytes[2] = (unsigned char)((value >> 16U) & 0xffU);
    bytes[3] = (unsigned char)((value >> 24U) & 0xffU);
    return minias_section_append(as, section, bytes, sizeof(bytes));
}

static bool require_reg(MiniAs *as, const MiniAsStmt *stmt, const char *text, int *out) {
    int value = reg_number(text);

    if (value < 0) {
        minias_set_error(as, "bad-register:%s:line=%zu", text, stmt->line);
        return false;
    }
    *out = value;
    return true;
}

static bool require_imm(MiniAs *as,
                        const MiniAsStmt *stmt,
                        const char *text,
                        int64_t *out) {
    if (!parse_i64(text, out)) {
        minias_set_error(as, "unsupported-expression:%s:line=%zu", text, stmt->line);
        return false;
    }
    return true;
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

bool minias_riscv_measure(const char *op,
                          const char *args,
                          uint32_t *size,
                          char *reason,
                          size_t reason_size) {
    char operands[8][128];
    size_t count = split_operands(args, operands, 8U);

    if (strcmp(op, ".insn") == 0) {
        uint32_t word;
        if (!parse_raw_insn_directive(args, &word)) {
            (void)snprintf(reason, reason_size, "bad-operands:.insn:%s", args);
            return false;
        }
        (void)word;
        *size = 4U;
        return true;
    }

    if (strcmp(op, "lb") == 0 || strcmp(op, "lbu") == 0 ||
        strcmp(op, "lh") == 0 || strcmp(op, "lhu") == 0 ||
        strcmp(op, "lw") == 0 || strcmp(op, "lwu") == 0 ||
        strcmp(op, "ld") == 0) {
        MiniAsSymbolExpr expr;
        if (count == 2U && reg_number(operands[0]) >= 0 &&
            minias_parse_symbol_addend(operands[1], &expr)) {
            *size = 8U;
            return true;
        }
    }

    if (strcmp(op, "c.li") == 0) {
        int64_t imm;
        int rd;
        if (count != 2U || (rd = reg_number(operands[0])) <= 0 ||
            !parse_i64(operands[1], &imm) || imm < -32 || imm > 31) {
            (void)snprintf(reason, reason_size, "bad-operands:c.li:%s", args);
            return false;
        }
        *size = 2U;
        return true;
    }

    if (strcmp(op, "fld") == 0 || strcmp(op, "flw") == 0 ||
        strcmp(op, "fsd") == 0 || strcmp(op, "fsw") == 0) {
        int64_t offset;
        char base[128];
        if (count != 2U || float_reg_number(operands[0]) < 0 ||
            !parse_mem(operands[1], &offset, base) ||
            reg_number(base) < 0 || offset < -2048 || offset > 2047) {
            (void)snprintf(reason, reason_size, "bad-operands:%s:%s", op, args);
            return false;
        }
        *size = 4U;
        return true;
    }

    if (strcmp(op, "li") == 0) {
        uint64_t bits;
        bool fits_signed32;
        int64_t signed_value;

        if (count != 2U ||
            !parse_li_bits(operands[1], &bits, &fits_signed32, &signed_value)) {
            (void)snprintf(reason,
                           reason_size,
                           "unsupported-expression:%s",
                           count >= 2U ? operands[1] : args);
            return false;
        }
        if (fits_signed32) {
            *size = signed_value >= -2048 && signed_value <= 2047 ? 4U : 8U;
        } else {
            *size = li64_materialized_size(bits);
        }
        return true;
    }
    if (strcmp(op, "lla") == 0 || strcmp(op, "la") == 0) {
        MiniAsSymbolExpr expr;
        if (count != 2U || reg_number(operands[0]) < 0 ||
            !minias_parse_symbol_addend(operands[1], &expr)) {
            (void)snprintf(reason, reason_size, "unsupported-expression:%s", args);
            return false;
        }
        *size = 8U;
        return true;
    }
    if (strcmp(op, "call") == 0) {
        MiniAsSymbolExpr expr;
        if (count != 1U || !parse_call_target(operands[0], &expr)) {
            (void)snprintf(reason, reason_size, "unsupported-expression:%s", args);
            return false;
        }
        *size = 8U;
        return true;
    }
    if (strcmp(op, "tail") == 0) {
        MiniAsSymbolExpr expr;
        if (count != 1U || !parse_call_target(operands[0], &expr)) {
            (void)snprintf(reason, reason_size, "unsupported-expression:%s", args);
            return false;
        }
        *size = 8U;
        return true;
    }

    {
        bool vector_store;
        uint32_t vector_width;
        if (decode_vector_unit_stride(op, &vector_store, &vector_width)) {
            int64_t offset;
            char base[128];
            bool mask_ok = count == 2U ||
                           (count == 3U && strcmp(operands[2], "v0.t") == 0);
            (void)vector_store;
            (void)vector_width;
            if (!mask_ok || vector_reg_number(operands[0]) < 0 ||
                !parse_mem(operands[1], &offset, base) || offset != 0 ||
                reg_number(base) < 0) {
                (void)snprintf(reason,
                               reason_size,
                               "bad-operands:%s:%s",
                               op,
                               args);
                return false;
            }
            *size = 4U;
            return true;
        }
    }

    if (strcmp(op, "orc.b") == 0 || strcmp(op, "rev8") == 0 ||
        strcmp(op, "ctz") == 0) {
        if (count != 2U || reg_number(operands[0]) < 0 ||
            reg_number(operands[1]) < 0) {
            (void)snprintf(reason, reason_size, "bad-operands:%s:%s", op, args);
            return false;
        }
        *size = 4U;
        return true;
    }

    if (strcmp(op, "vmv.v.i") == 0) {
        int64_t imm;
        if (count != 2U || vector_reg_number(operands[0]) < 0 ||
            !parse_i64(operands[1], &imm) || imm < -16 || imm > 15) {
            (void)snprintf(reason, reason_size, "bad-operands:vmv.v.i:%s", args);
            return false;
        }
        *size = 4U;
        return true;
    }

    if (strcmp(op, "vsetvli") == 0) {
        uint32_t vtypei;
        if ((count != 5U && count != 6U) ||
            reg_number(operands[0]) < 0 ||
            reg_number(operands[1]) < 0 ||
            !parse_vsetvli_vtype(operands, count, &vtypei)) {
            (void)snprintf(reason, reason_size, "bad-operands:vsetvli:%s", args);
            return false;
        }
        (void)vtypei;
        *size = 4U;
        return true;
    }

    if (strcmp(op, "sfence.vma") == 0) {
        if (count > 2U ||
            (count >= 1U && reg_number(operands[0]) < 0) ||
            (count == 2U && reg_number(operands[1]) < 0)) {
            (void)snprintf(reason, reason_size, "bad-operands:sfence.vma:%s", args);
            return false;
        }
        *size = 4U;
        return true;
    }

    {
        bool is_lr;
        uint32_t atomic_width;
        uint32_t atomic_ordering;
        if (decode_lr_sc_mnemonic(op, &is_lr, &atomic_width, &atomic_ordering)) {
            (void)is_lr;
            (void)atomic_width;
            (void)atomic_ordering;
            *size = 4U;
            return true;
        }
    }

    {
        uint32_t amo_funct5;
        uint32_t amo_width;
        uint32_t amo_ordering;
        if (decode_amo_mnemonic(op, &amo_funct5, &amo_width, &amo_ordering)) {
            (void)amo_funct5;
            (void)amo_width;
            (void)amo_ordering;
            *size = 4U;
            return true;
        }
    }

#define SIMPLE(OP) strcmp(op, OP) == 0
    if (SIMPLE("ret") || SIMPLE("nop") || SIMPLE("mv") || SIMPLE("move") || SIMPLE("jr") ||
        SIMPLE("snez") || SIMPLE("seqz") || SIMPLE("neg") || SIMPLE("negw") || SIMPLE("not") || SIMPLE("frcsr") || SIMPLE("fscsr") || SIMPLE("sext.w") || SIMPLE("jalr") ||
        SIMPLE("j") || SIMPLE("jal") || SIMPLE("beq") || SIMPLE("bne") ||
        SIMPLE("blt") || SIMPLE("bge") || SIMPLE("bltu") || SIMPLE("bgeu") ||
        SIMPLE("beqz") || SIMPLE("bnez") || SIMPLE("bltz") || SIMPLE("bgez") ||
        SIMPLE("bgtz") || SIMPLE("blez") || SIMPLE("bgt") || SIMPLE("ble") ||
        SIMPLE("bgtu") || SIMPLE("bleu") || SIMPLE("addi") || SIMPLE("addiw") ||
        SIMPLE("andi") || SIMPLE("ori") || SIMPLE("xori") || SIMPLE("slti") ||
        SIMPLE("sltiu") || SIMPLE("slli") || SIMPLE("srli") || SIMPLE("srai") ||
        SIMPLE("slliw") || SIMPLE("srliw") || SIMPLE("sraiw") ||
        SIMPLE("sra") || SIMPLE("fence") || SIMPLE("fence.i") || SIMPLE("vsetvl") || SIMPLE("sret") ||
        SIMPLE("csrr") || SIMPLE("csrrw") || SIMPLE("csrrc") || SIMPLE("csrw") ||
        SIMPLE("csrs") || SIMPLE("csrc") || SIMPLE("ecall") || SIMPLE("ebreak") ||
        SIMPLE("pause") ||
        SIMPLE("wfi") || SIMPLE("add") || SIMPLE("sub") || SIMPLE("sll") ||
        SIMPLE("srl") || SIMPLE("and") || SIMPLE("or") ||
        SIMPLE("xor") || SIMPLE("slt") || SIMPLE("sltu") || SIMPLE("mul") ||
        SIMPLE("mulh") || SIMPLE("mulhu") || SIMPLE("div") || SIMPLE("divu") ||
        SIMPLE("rem") || SIMPLE("remu") || SIMPLE("addw") || SIMPLE("subw") ||
        SIMPLE("sllw") || SIMPLE("srlw") || SIMPLE("sraw") || SIMPLE("mulw") ||
        SIMPLE("divw") || SIMPLE("divuw") || SIMPLE("remw") || SIMPLE("remuw") ||
        SIMPLE("lb") || SIMPLE("lbu") ||
        SIMPLE("lh") || SIMPLE("lhu") || SIMPLE("lw") || SIMPLE("lwu") ||
        SIMPLE("ld") || SIMPLE("sb") || SIMPLE("sh") || SIMPLE("sw") ||
        SIMPLE("sd") || SIMPLE("lui") || SIMPLE("auipc")) {
        *size = 4U;
        return true;
    }
#undef SIMPLE

    (void)snprintf(reason, reason_size, "unsupported-instruction:%s", op);
    return false;
}

bool minias_riscv_encode(MiniAs *as, const MiniAsStmt *stmt) {
    char operands[8][128];
    size_t count = split_operands(stmt->args, operands, 8U);
    if (strcmp(stmt->op, "orc.b") == 0 ||
        strcmp(stmt->op, "rev8") == 0 ||
        strcmp(stmt->op, "ctz") == 0) {
        int64_t encoded_imm;
        uint32_t encoded_funct3;
        int encoded_rd;
        int encoded_rs1;

        if (count != 2U ||
            !require_reg(as, stmt, operands[0], &encoded_rd) ||
            !require_reg(as, stmt, operands[1], &encoded_rs1)) {
            return false;
        }
        if (strcmp(stmt->op, "orc.b") == 0) {
            encoded_imm = 0x287;
            encoded_funct3 = 5U;
        } else if (strcmp(stmt->op, "rev8") == 0) {
            encoded_imm = 0x6b8;
            encoded_funct3 = 5U;
        } else {
            encoded_imm = 0x601;
            encoded_funct3 = 1U;
        }
        return append_u32(as,
                          stmt->section,
                          enc_i(0x13U,
                                encoded_rd,
                                encoded_funct3,
                                encoded_rs1,
                                encoded_imm));
    }

    if (strcmp(stmt->op, "c.li") == 0) {
        int64_t imm;
        int compressed_rd;
        uint16_t encoded;

        if (count != 2U ||
            (compressed_rd = reg_number(operands[0])) <= 0 ||
            !require_imm(as, stmt, operands[1], &imm) ||
            imm < -32 || imm > 31) {
            minias_set_error(as, "bad-operands:c.li:%s:line=%zu",
                             stmt->args,
                             stmt->line);
            return false;
        }
        encoded = (uint16_t)((2U << 13U) |
                             ((((uint16_t)imm >> 5U) & 1U) << 12U) |
                             ((uint16_t)compressed_rd << 7U) |
                             (((uint16_t)imm & 0x1fU) << 2U) |
                             1U);
        return append_u16(as, stmt->section, encoded);
    }

    if (strcmp(stmt->op, ".insn") == 0) {
        uint32_t raw_word;
        if (!parse_raw_insn_directive(stmt->args, &raw_word)) {
            minias_set_error(as, "bad-operands:.insn:%s:line=%zu",
                             stmt->args,
                             stmt->line);
            return false;
        }
        return append_u32(as, stmt->section, raw_word);
    }
    int rd;
    int rs1;
    int rs2;
    int64_t immediate;
    uint32_t value = 0U;
    uint32_t funct3;
    MiniAsSymbol *symbol;

    {
        bool vector_store;
        uint32_t vector_width;
        if (decode_vector_unit_stride(stmt->op, &vector_store, &vector_width)) {
            int vector_reg;
            char base[128];
            bool masked;
            uint32_t vm;

            if ((count != 2U && count != 3U) ||
                (count == 3U && strcmp(operands[2], "v0.t") != 0)) {
                minias_set_error(as,
                                 "bad-operands:%s:%s:line=%zu",
                                 stmt->op,
                                 stmt->args,
                                 stmt->line);
                return false;
            }
            vector_reg = vector_reg_number(operands[0]);
            if (vector_reg < 0 || !parse_mem(operands[1], &immediate, base) ||
                immediate != 0 || !require_reg(as, stmt, base, &rs1)) {
                minias_set_error(as,
                                 "bad-operands:%s:%s:line=%zu",
                                 stmt->op,
                                 stmt->args,
                                 stmt->line);
                return false;
            }
            masked = count == 3U;
            vm = masked ? 0U : 1U;
            value = (vm << 25U) | ((uint32_t)rs1 << 15U) |
                    (vector_width << 12U) | ((uint32_t)vector_reg << 7U) |
                    (vector_store ? 0x27U : 0x07U);
            return append_u32(as, stmt->section, value);
        }
    }

    if (strcmp(stmt->op, "vmv.v.i") == 0) {
        int vector_reg;
        if (count != 2U) {
            minias_set_error(as, "bad-operands:vmv.v.i:%s:line=%zu",
                             stmt->args,
                             stmt->line);
            return false;
        }
        vector_reg = vector_reg_number(operands[0]);
        if (vector_reg < 0 ||
            !require_imm(as, stmt, operands[1], &immediate) ||
            immediate < -16 || immediate > 15) {
            minias_set_error(as, "bad-operands:vmv.v.i:%s:line=%zu",
                             stmt->args,
                             stmt->line);
            return false;
        }
        value = (0x17U << 26U) | (1U << 25U) |
                (((uint32_t)immediate & 0x1fU) << 15U) |
                (3U << 12U) | ((uint32_t)vector_reg << 7U) | 0x57U;
        return append_u32(as, stmt->section, value);
    }

    if (strcmp(stmt->op, "vsetvli") == 0) {
        uint32_t vtypei;
        if (!require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1) ||
            !parse_vsetvli_vtype(operands, count, &vtypei)) {
            minias_set_error(as, "bad-operands:vsetvli:%s:line=%zu",
                             stmt->args,
                             stmt->line);
            return false;
        }
        value = (vtypei << 20U) | ((uint32_t)rs1 << 15U) |
                (7U << 12U) | ((uint32_t)rd << 7U) | 0x57U;
        return append_u32(as, stmt->section, value);
    }

    if (strcmp(stmt->op, "vsetvl") == 0) {
        if (count != 3U ||
            !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1) ||
            !require_reg(as, stmt, operands[2], &rs2)) {
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          enc_r(0x57U, rd, 7U, rs1, rs2, 0x40U));
    }
    if (strcmp(stmt->op, "fld") == 0 || strcmp(stmt->op, "flw") == 0 ||
        strcmp(stmt->op, "fsd") == 0 || strcmp(stmt->op, "fsw") == 0) {
        int freg;
        char base[128];
        int64_t offset;
        uint32_t width;

        if (count != 2U || (freg = float_reg_number(operands[0])) < 0 ||
            !parse_mem(operands[1], &offset, base) ||
            !require_reg(as, stmt, base, &rs1) ||
            offset < -2048 || offset > 2047) {
            minias_set_error(as, "bad-operands:%s:%s:line=%zu",
                             stmt->op,
                             stmt->args,
                             stmt->line);
            return false;
        }
        width = (strcmp(stmt->op, "fld") == 0 || strcmp(stmt->op, "fsd") == 0)
                    ? 3U
                    : 2U;
        if (stmt->op[1] == 'l') {
            return append_u32(as,
                              stmt->section,
                              enc_i(0x07U, freg, width, rs1, offset));
        }
        return append_u32(as,
                          stmt->section,
                          enc_s(width, rs1, freg, offset));
    }

    if (strcmp(stmt->op, "sfence.vma") == 0) {
        rs1 = 0;
        rs2 = 0;
        if (count > 2U ||
            (count >= 1U && !require_reg(as, stmt, operands[0], &rs1)) ||
            (count == 2U && !require_reg(as, stmt, operands[1], &rs2))) {
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          0x12000073U | ((uint32_t)rs1 << 15U) |
                              ((uint32_t)rs2 << 20U));
    }
    if (strcmp(stmt->op, "ret") == 0) {
        return append_u32(as, stmt->section, enc_i(0x67U, 0, 0U, 1, 0));
    }
    if (strcmp(stmt->op, "nop") == 0) {
        return append_u32(as, stmt->section, enc_i(0x13U, 0, 0U, 0, 0));
    }
    if (strcmp(stmt->op, "mv") == 0 || strcmp(stmt->op, "move") == 0) {
        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            return false;
        }
        return append_u32(as, stmt->section, enc_i(0x13U, rd, 0U, rs1, 0));
    }
    if (strcmp(stmt->op, "jr") == 0) {
        if (count != 1U || !require_reg(as, stmt, operands[0], &rs1)) {
            return false;
        }
        return append_u32(as, stmt->section, enc_i(0x67U, 0, 0U, rs1, 0));
    }
    if (strcmp(stmt->op, "snez") == 0) {
        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            return false;
        }
        return append_u32(as, stmt->section, enc_r(0x33U, rd, 3U, 0, rs1, 0U));
    }
    if (strcmp(stmt->op, "seqz") == 0) {
        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            return false;
        }
        return append_u32(as, stmt->section, enc_i(0x13U, rd, 3U, rs1, 1));
    }
    if (strcmp(stmt->op, "neg") == 0) {
        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            return false;
        }
        return append_u32(as, stmt->section, enc_r(0x33U, rd, 0U, 0, rs1, 0x20U));
    }
    if (strcmp(stmt->op, "negw") == 0) {
        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            return false;
        }
        return append_u32(as, stmt->section, enc_r(0x3bU, rd, 0U, 0, rs1, 0x20U));
    }
    if (strcmp(stmt->op, "not") == 0) {
        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            return false;
        }
        return append_u32(as, stmt->section, enc_i(0x13U, rd, 4U, rs1, -1));
    }
    if (strcmp(stmt->op, "frcsr") == 0) {
        if (count != 1U || !require_reg(as, stmt, operands[0], &rd)) {
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          0x73U | ((uint32_t)rd << 7U) | (2U << 12U) |
                              (0x003U << 20U));
    }
    if (strcmp(stmt->op, "fscsr") == 0) {
        if (count != 1U || !require_reg(as, stmt, operands[0], &rs1)) {
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          0x73U | (1U << 12U) |
                              ((uint32_t)rs1 << 15U) | (0x003U << 20U));
    }
    if (strcmp(stmt->op, "li") == 0) {
        uint64_t bits;
        bool fits_signed32;
        int64_t signed_value;

        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !parse_li_bits(operands[1], &bits, &fits_signed32, &signed_value)) {
            return false;
        }
        if (fits_signed32) {
            int64_t hi;
            int64_t lo;

            if (signed_value >= -2048 && signed_value <= 2047) {
                return append_u32(as,
                                  stmt->section,
                                  enc_i(0x13U, rd, 0U, 0, signed_value));
            }
            hi = (signed_value + 0x800) >> 12;
            lo = signed_value - (hi << 12);
            if (!append_u32(as,
                            stmt->section,
                            0x37U | ((uint32_t)rd << 7U) |
                                (((uint32_t)hi & 0xfffffU) << 12U))) {
                return false;
            }
            return append_u32(as,
                              stmt->section,
                              enc_i(0x1bU, rd, 0U, rd, lo));
        }
        {
            int byte_index = 7;
            int i;
            uint64_t top;

            while (byte_index > 0 &&
                   ((bits >> ((unsigned int)byte_index * 8U)) &
                    UINT64_C(0xff)) == 0U) {
                --byte_index;
            }
            top = (bits >> ((unsigned int)byte_index * 8U)) & UINT64_C(0xff);
            if (!append_u32(as,
                            stmt->section,
                            enc_i(0x13U, rd, 0U, 0, (int64_t)top))) {
                return false;
            }
            for (i = byte_index - 1; i >= 0; --i) {
                uint64_t byte =
                    (bits >> ((unsigned int)i * 8U)) & UINT64_C(0xff);

                if (!append_u32(as,
                                stmt->section,
                                enc_i(0x13U, rd, 1U, rd, 8))) {
                    return false;
                }
                if (byte != 0U &&
                    !append_u32(as,
                                stmt->section,
                                enc_i(0x13U, rd, 0U, rd, (int64_t)byte))) {
                    return false;
                }
            }
            return true;
        }
    }
    if (strcmp(stmt->op, "call") == 0) {
        MiniAsSymbolExpr expr;

        if (count != 1U || !parse_call_target(operands[0], &expr)) {
            minias_set_error(as,
                             "unsupported-expression:%s:line=%zu",
                             stmt->args,
                             stmt->line);
            return false;
        }
        if (!append_u32(as, stmt->section, 0x00000097U) ||
            !append_u32(as, stmt->section, enc_i(0x67U, 1, 0U, 1, 0))) {
            return false;
        }
        return minias_add_relocation(as,
                                     stmt->section,
                                     stmt->offset,
                                     MINIAS_R_RISCV_CALL_PLT,
                                     expr.name,
                                     expr.addend);
    }
    if (strcmp(stmt->op, "tail") == 0) {
        MiniAsSymbolExpr expr;

        if (count != 1U || !parse_call_target(operands[0], &expr)) {
            minias_set_error(as,
                             "unsupported-expression:%s:line=%zu",
                             stmt->args,
                             stmt->line);
            return false;
        }
        if (!append_u32(as, stmt->section, 0x00000317U) ||
            !append_u32(as, stmt->section, enc_i(0x67U, 0, 0U, 6, 0))) {
            return false;
        }
        return minias_add_relocation(as,
                                     stmt->section,
                                     stmt->offset,
                                     MINIAS_R_RISCV_CALL_PLT,
                                     expr.name,
                                     expr.addend);
    }

    if (strcmp(stmt->op, "lla") == 0 || strcmp(stmt->op, "la") == 0) {
        MiniAsSymbolExpr expr;
        MiniAsSymbol *anchor;
        char anchor_name[96];

        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !minias_parse_symbol_addend(operands[1], &expr)) {
            minias_set_error(as, "unsupported-expression:%s:line=%zu", stmt->args, stmt->line);
            return false;
        }
        (void)snprintf(anchor_name,
                       sizeof(anchor_name),
                       ".Lminias_pcrel_%zu",
                       ++as->pcrel_anchor_counter);
        anchor = minias_get_symbol(as, anchor_name, true);
        if (anchor == NULL) {
            return false;
        }
        anchor->defined = true;
        anchor->section = stmt->section;
        anchor->value = stmt->offset;
        anchor->bind = MINIAS_STB_LOCAL;

        if (!append_u32(as, stmt->section, 0x17U | ((uint32_t)rd << 7U)) ||
            !append_u32(as, stmt->section, enc_i(0x13U, rd, 0U, rd, 0))) {
            return false;
        }
        return minias_add_relocation(as,
                                     stmt->section,
                                     stmt->offset,
                                     MINIAS_R_RISCV_PCREL_HI20,
                                     expr.name,
                                     expr.addend) &&
               minias_add_relocation(as,
                                     stmt->section,
                                     stmt->offset + 4U,
                                     MINIAS_R_RISCV_PCREL_LO12_I,
                                     anchor_name,
                                     0);
    }

    if (strcmp(stmt->op, "lui") == 0 || strcmp(stmt->op, "auipc") == 0) {
        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_imm(as, stmt, operands[1], &immediate)) {
            return false;
        }
        value = (strcmp(stmt->op, "lui") == 0 ? 0x37U : 0x17U) |
                ((uint32_t)rd << 7U) |
                (((uint32_t)immediate & 0xfffffU) << 12U);
        return append_u32(as, stmt->section, value);
    }
    if (strcmp(stmt->op, "sext.w") == 0) {
        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          enc_i(0x1bU, rd, 0U, rs1, 0));
    }
    if (strcmp(stmt->op, "addi") == 0 || strcmp(stmt->op, "addiw") == 0 ||
        strcmp(stmt->op, "andi") == 0 || strcmp(stmt->op, "ori") == 0 ||
        strcmp(stmt->op, "xori") == 0 || strcmp(stmt->op, "slti") == 0 ||
        strcmp(stmt->op, "sltiu") == 0) {
        if (count != 3U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1) ||
            !require_imm(as, stmt, operands[2], &immediate)) {
            return false;
        }
        if (immediate < -2048 || immediate > 2047) {
            minias_set_error(as, "immediate-range:%s:line=%zu", stmt->op, stmt->line);
            return false;
        }
        funct3 = strcmp(stmt->op, "slti") == 0    ? 2U
                 : strcmp(stmt->op, "sltiu") == 0 ? 3U
                 : strcmp(stmt->op, "xori") == 0  ? 4U
                 : strcmp(stmt->op, "ori") == 0   ? 6U
                 : strcmp(stmt->op, "andi") == 0  ? 7U
                                                   : 0U;
        return append_u32(as,
                          stmt->section,
                          enc_i(strcmp(stmt->op, "addiw") == 0 ? 0x1bU : 0x13U,
                                rd,
                                funct3,
                                rs1,
                                immediate));
    }
    if (strcmp(stmt->op, "pause") == 0) {
        if (count != 0U) {
            minias_set_error(as, "operand-count:pause:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as, stmt->section, 0x0100000fU);
    }
    if (strcmp(stmt->op, "wfi") == 0) {
        if (count != 0U) {
            minias_set_error(as, "operand-count:wfi:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as, stmt->section, 0x10500073U);
    }
    {
        bool is_lr;
        uint32_t atomic_width;
        uint32_t atomic_ordering;
        if (decode_lr_sc_mnemonic(stmt->op,
                                  &is_lr,
                                  &atomic_width,
                                  &atomic_ordering)) {
            char base[128];
            int64_t offset;
            uint32_t funct5 = is_lr ? 0x02U : 0x03U;

            if (is_lr) {
                if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
                    !parse_mem(operands[1], &offset, base) || offset != 0 ||
                    !require_reg(as, stmt, base, &rs1)) {
                    minias_set_error(as, "bad-%s:line=%zu", stmt->op, stmt->line);
                    return false;
                }
                rs2 = 0;
            } else {
                if (count != 3U || !require_reg(as, stmt, operands[0], &rd) ||
                    !require_reg(as, stmt, operands[1], &rs2) ||
                    !parse_mem(operands[2], &offset, base) || offset != 0 ||
                    !require_reg(as, stmt, base, &rs1)) {
                    minias_set_error(as, "bad-%s:line=%zu", stmt->op, stmt->line);
                    return false;
                }
            }
            return append_u32(as,
                              stmt->section,
                              0x2fU | ((uint32_t)rd << 7U) |
                                  (atomic_width << 12U) |
                                  ((uint32_t)rs1 << 15U) |
                                  ((uint32_t)rs2 << 20U) |
                                  atomic_ordering | (funct5 << 27U));
        }
    }

    {
        uint32_t amo_funct5;
        uint32_t amo_width;
        uint32_t amo_ordering;
        if (decode_amo_mnemonic(stmt->op,
                                &amo_funct5,
                                &amo_width,
                                &amo_ordering)) {
            char base[128];
            int64_t offset;

            if (count != 3U || !require_reg(as, stmt, operands[0], &rd) ||
                !require_reg(as, stmt, operands[1], &rs2) ||
                !parse_mem(operands[2], &offset, base) || offset != 0 ||
                !require_reg(as, stmt, base, &rs1)) {
                minias_set_error(as, "bad-%s:line=%zu", stmt->op, stmt->line);
                return false;
            }
            return append_u32(as,
                              stmt->section,
                              0x2fU | ((uint32_t)rd << 7U) |
                                  (amo_width << 12U) |
                                  ((uint32_t)rs1 << 15U) |
                                  ((uint32_t)rs2 << 20U) |
                                  amo_ordering | (amo_funct5 << 27U));
        }
    }
    if (strcmp(stmt->op, "sret") == 0) {
        if (count != 0U) {
            minias_set_error(as, "operand-count:sret:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as, stmt->section, 0x10200073U);
    }
    if (strcmp(stmt->op, "ecall") == 0) {
        if (count != 0U) {
            minias_set_error(as, "operand-count:ecall:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as, stmt->section, 0x00000073U);
    }
    if (strcmp(stmt->op, "ebreak") == 0) {
        if (count != 0U) {
            minias_set_error(as, "operand-count:ebreak:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as, stmt->section, 0x00100073U);
    }
    if (strcmp(stmt->op, "csrr") == 0) {
        uint32_t csr;

        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !parse_csr(operands[1], &csr)) {
            minias_set_error(as, "bad-csrr:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          0x73U | ((uint32_t)rd << 7U) | (2U << 12U) |
                              (csr << 20U));
    }
    if (strcmp(stmt->op, "csrrw") == 0) {
        uint32_t csr;

        if (count != 3U || !require_reg(as, stmt, operands[0], &rd) ||
            !parse_csr(operands[1], &csr) ||
            !require_reg(as, stmt, operands[2], &rs1)) {
            minias_set_error(as, "bad-csrrw:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          0x73U | ((uint32_t)rd << 7U) | (1U << 12U) |
                              ((uint32_t)rs1 << 15U) | (csr << 20U));
    }
    if (strcmp(stmt->op, "csrrc") == 0) {
        uint32_t csr;

        if (count != 3U || !require_reg(as, stmt, operands[0], &rd) ||
            !parse_csr(operands[1], &csr) ||
            !require_reg(as, stmt, operands[2], &rs1)) {
            minias_set_error(as, "bad-csrrc:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          0x73U | ((uint32_t)rd << 7U) | (3U << 12U) |
                              ((uint32_t)rs1 << 15U) | (csr << 20U));
    }
    if (strcmp(stmt->op, "csrw") == 0) {
        uint32_t csr;

        if (count != 2U || !parse_csr(operands[0], &csr) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            minias_set_error(as, "bad-csrw:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          0x73U | (1U << 12U) |
                              ((uint32_t)rs1 << 15U) | (csr << 20U));
    }
    if (strcmp(stmt->op, "csrs") == 0) {
        uint32_t csr;

        if (count != 2U || !parse_csr(operands[0], &csr) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            minias_set_error(as, "bad-csrs:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          0x73U | (2U << 12U) |
                              ((uint32_t)rs1 << 15U) | (csr << 20U));
    }
    if (strcmp(stmt->op, "csrc") == 0) {
        uint32_t csr;

        if (count != 2U || !parse_csr(operands[0], &csr) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            minias_set_error(as, "bad-csrc:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          0x73U | (3U << 12U) |
                              ((uint32_t)rs1 << 15U) | (csr << 20U));
    }
    if (strcmp(stmt->op, "fence.i") == 0) {
        if (count != 0U) {
            minias_set_error(as, "operand-count:fence.i:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as, stmt->section, 0x0000100fU);
    }
    if (strcmp(stmt->op, "fence") == 0) {
        uint32_t pred = 0xfU;
        uint32_t succ = 0xfU;

        if (count == 2U) {
            if (!parse_fence_set(operands[0], &pred) ||
                !parse_fence_set(operands[1], &succ)) {
                minias_set_error(as, "bad-fence:line=%zu", stmt->line);
                return false;
            }
        } else if (count != 0U) {
            minias_set_error(as, "operand-count:fence:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          enc_i(0x0fU,
                                0,
                                0U,
                                0,
                                (int64_t)((pred << 4U) | succ)));
    }
    if (strcmp(stmt->op, "slliw") == 0 || strcmp(stmt->op, "srliw") == 0 ||
        strcmp(stmt->op, "sraiw") == 0) {
        if (count != 3U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1) ||
            !require_imm(as, stmt, operands[2], &immediate)) {
            return false;
        }
        if (immediate < 0 || immediate > 31) {
            minias_set_error(as, "shift-range:%s:line=%zu", stmt->op, stmt->line);
            return false;
        }
        value = enc_i(0x1bU,
                      rd,
                      strcmp(stmt->op, "slliw") == 0 ? 1U : 5U,
                      rs1,
                      immediate);
        if (strcmp(stmt->op, "sraiw") == 0) {
            value |= 0x40000000U;
        }
        return append_u32(as, stmt->section, value);
    }
    if (strcmp(stmt->op, "slli") == 0 || strcmp(stmt->op, "srli") == 0 ||
        strcmp(stmt->op, "srai") == 0) {
        if (count != 3U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1) ||
            !require_imm(as, stmt, operands[2], &immediate)) {
            return false;
        }
        if (immediate < 0 || immediate > 63) {
            minias_set_error(as, "shift-range:%s:line=%zu", stmt->op, stmt->line);
            return false;
        }
        value = enc_i(0x13U,
                      rd,
                      strcmp(stmt->op, "slli") == 0 ? 1U : 5U,
                      rs1,
                      immediate);
        if (strcmp(stmt->op, "srai") == 0) {
            value |= 0x40000000U;
        }
        return append_u32(as, stmt->section, value);
    }
    if (strcmp(stmt->op, "addw") == 0 || strcmp(stmt->op, "subw") == 0 ||
        strcmp(stmt->op, "sllw") == 0 || strcmp(stmt->op, "srlw") == 0 ||
        strcmp(stmt->op, "sraw") == 0 || strcmp(stmt->op, "mulw") == 0 ||
        strcmp(stmt->op, "divw") == 0 || strcmp(stmt->op, "divuw") == 0 ||
        strcmp(stmt->op, "remw") == 0 || strcmp(stmt->op, "remuw") == 0) {
        uint32_t word_funct3 = 0U;
        uint32_t word_funct7 = 0U;

        if (count != 3U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1) ||
            !require_reg(as, stmt, operands[2], &rs2)) {
            return false;
        }
        if (strcmp(stmt->op, "subw") == 0) {
            word_funct7 = 0x20U;
        } else if (strcmp(stmt->op, "sllw") == 0) {
            word_funct3 = 1U;
        } else if (strcmp(stmt->op, "srlw") == 0) {
            word_funct3 = 5U;
        } else if (strcmp(stmt->op, "sraw") == 0) {
            word_funct3 = 5U;
            word_funct7 = 0x20U;
        } else if (strcmp(stmt->op, "mulw") == 0) {
            word_funct7 = 1U;
        } else if (strcmp(stmt->op, "divw") == 0) {
            word_funct3 = 4U;
            word_funct7 = 1U;
        } else if (strcmp(stmt->op, "divuw") == 0) {
            word_funct3 = 5U;
            word_funct7 = 1U;
        } else if (strcmp(stmt->op, "remw") == 0) {
            word_funct3 = 6U;
            word_funct7 = 1U;
        } else if (strcmp(stmt->op, "remuw") == 0) {
            word_funct3 = 7U;
            word_funct7 = 1U;
        }
        return append_u32(as,
                          stmt->section,
                          enc_r(0x3bU,
                                rd,
                                word_funct3,
                                rs1,
                                rs2,
                                word_funct7));
    }
    if (strcmp(stmt->op, "add") == 0 || strcmp(stmt->op, "sub") == 0 ||
        strcmp(stmt->op, "sll") == 0 || strcmp(stmt->op, "srl") == 0 ||
        strcmp(stmt->op, "sra") == 0 || strcmp(stmt->op, "and") == 0 ||
        strcmp(stmt->op, "or") == 0 ||
        strcmp(stmt->op, "xor") == 0 || strcmp(stmt->op, "slt") == 0 ||
        strcmp(stmt->op, "sltu") == 0 || strcmp(stmt->op, "mul") == 0 ||
        strcmp(stmt->op, "mulh") == 0 || strcmp(stmt->op, "mulhu") == 0 ||
        strcmp(stmt->op, "div") == 0 || strcmp(stmt->op, "divu") == 0 ||
        strcmp(stmt->op, "rem") == 0 || strcmp(stmt->op, "remu") == 0) {
        uint32_t funct7 = 0U;

        if (count != 3U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1)) {
            return false;
        }
        if ((strcmp(stmt->op, "sll") == 0 || strcmp(stmt->op, "srl") == 0 ||
             strcmp(stmt->op, "sra") == 0) &&
            parse_i64(operands[2], &immediate)) {
            if (immediate < 0 || immediate > 63) {
                minias_set_error(as, "shift-range:%s:line=%zu", stmt->op, stmt->line);
                return false;
            }
            value = enc_i(0x13U,
                          rd,
                          strcmp(stmt->op, "sll") == 0 ? 1U : 5U,
                          rs1,
                          immediate);
            if (strcmp(stmt->op, "sra") == 0) {
                value |= 0x40000000U;
            }
            return append_u32(as, stmt->section, value);
        }
        if ((strcmp(stmt->op, "add") == 0 ||
             strcmp(stmt->op, "sub") == 0) &&
            reg_number(operands[2]) < 0 &&
            parse_i64(operands[2], &immediate)) {
            if (strcmp(stmt->op, "sub") == 0) {
                if (immediate == INT64_MIN) {
                    minias_set_error(as,
                                     "immediate-range:sub:%s:line=%zu",
                                     operands[2],
                                     stmt->line);
                    return false;
                }
                immediate = -immediate;
            }
            if (immediate < -2048 || immediate > 2047) {
                minias_set_error(as,
                                 "immediate-range:%s:%s:line=%zu",
                                 stmt->op,
                                 operands[2],
                                 stmt->line);
                return false;
            }
            return append_u32(as,
                              stmt->section,
                              enc_i(0x13U,
                                    rd,
                                    0U,
                                    rs1,
                                    immediate));
        }
        if ((strcmp(stmt->op, "and") == 0 ||
             strcmp(stmt->op, "or") == 0 ||
             strcmp(stmt->op, "xor") == 0) &&
            parse_i64(operands[2], &immediate)) {
            uint32_t imm_funct3 =
                strcmp(stmt->op, "and") == 0 ? 7U
                : strcmp(stmt->op, "or") == 0 ? 6U
                                               : 4U;
            if (immediate < -2048 || immediate > 2047) {
                minias_set_error(as,
                                 "immediate-range:%s:%s:line=%zu",
                                 stmt->op,
                                 operands[2],
                                 stmt->line);
                return false;
            }
            return append_u32(as,
                              stmt->section,
                              enc_i(0x13U,
                                    rd,
                                    imm_funct3,
                                    rs1,
                                    immediate));
        }
        if (!require_reg(as, stmt, operands[2], &rs2)) {
            return false;
        }
        if (strcmp(stmt->op, "sub") == 0) {
            funct3 = 0U;
            funct7 = 0x20U;
        } else if (strcmp(stmt->op, "sll") == 0) {
            funct3 = 1U;
        } else if (strcmp(stmt->op, "srl") == 0) {
            funct3 = 5U;
        } else if (strcmp(stmt->op, "sra") == 0) {
            funct3 = 5U;
            funct7 = 0x20U;
        } else if (strcmp(stmt->op, "and") == 0) {
            funct3 = 7U;
        } else if (strcmp(stmt->op, "or") == 0) {
            funct3 = 6U;
        } else if (strcmp(stmt->op, "xor") == 0) {
            funct3 = 4U;
        } else if (strcmp(stmt->op, "slt") == 0) {
            funct3 = 2U;
        } else if (strcmp(stmt->op, "sltu") == 0) {
            funct3 = 3U;
        } else if (strcmp(stmt->op, "mul") == 0) {
            funct3 = 0U;
            funct7 = 1U;
        } else if (strcmp(stmt->op, "mulh") == 0) {
            funct3 = 1U;
            funct7 = 1U;
        } else if (strcmp(stmt->op, "mulhu") == 0) {
            funct3 = 3U;
            funct7 = 1U;
        } else if (strcmp(stmt->op, "div") == 0) {
            funct3 = 4U;
            funct7 = 1U;
        } else if (strcmp(stmt->op, "divu") == 0) {
            funct3 = 5U;
            funct7 = 1U;
        } else if (strcmp(stmt->op, "rem") == 0) {
            funct3 = 6U;
            funct7 = 1U;
        } else if (strcmp(stmt->op, "remu") == 0) {
            funct3 = 7U;
            funct7 = 1U;
        } else {
            funct3 = 0U;
        }
        return append_u32(as, stmt->section, enc_r(0x33U, rd, funct3, rs1, rs2, funct7));
    }
    if (strcmp(stmt->op, "lb") == 0 || strcmp(stmt->op, "lbu") == 0 ||
        strcmp(stmt->op, "lh") == 0 || strcmp(stmt->op, "lhu") == 0 ||
        strcmp(stmt->op, "lw") == 0 || strcmp(stmt->op, "lwu") == 0 ||
        strcmp(stmt->op, "ld") == 0) {
        char base[128];

        if (count != 2U || !require_reg(as, stmt, operands[0], &rd)) {
            return false;
        }
        funct3 = strcmp(stmt->op, "lb") == 0    ? 0U
                 : strcmp(stmt->op, "lh") == 0  ? 1U
                 : strcmp(stmt->op, "lw") == 0  ? 2U
                 : strcmp(stmt->op, "ld") == 0  ? 3U
                 : strcmp(stmt->op, "lbu") == 0 ? 4U
                 : strcmp(stmt->op, "lhu") == 0 ? 5U
                                                 : 6U;
        if (parse_mem(operands[1], &immediate, base)) {
            if (!require_reg(as, stmt, base, &rs1)) {
                return false;
            }
            return append_u32(as,
                              stmt->section,
                              enc_i(0x03U, rd, funct3, rs1, immediate));
        }
        {
            MiniAsSymbolExpr expr;
            MiniAsSymbol *anchor;
            char anchor_name[96];

            if (!minias_parse_symbol_addend(operands[1], &expr)) {
                minias_set_error(as,
                                 "unsupported-expression:%s:%s:line=%zu",
                                 stmt->op,
                                 operands[1],
                                 stmt->line);
                return false;
            }
            (void)snprintf(anchor_name,
                           sizeof(anchor_name),
                           ".Lminias_load_%zu",
                           ++as->pcrel_anchor_counter);
            anchor = minias_get_symbol(as, anchor_name, true);
            if (anchor == NULL) {
                return false;
            }
            anchor->defined = true;
            anchor->section = stmt->section;
            anchor->subsection = stmt->subsection;
            anchor->value = stmt->offset;
            anchor->bind = MINIAS_STB_LOCAL;

            if (!append_u32(as,
                            stmt->section,
                            0x17U | ((uint32_t)rd << 7U)) ||
                !append_u32(as,
                            stmt->section,
                            enc_i(0x03U, rd, funct3, rd, 0))) {
                return false;
            }
            return minias_add_relocation(as,
                                         stmt->section,
                                         stmt->offset,
                                         MINIAS_R_RISCV_PCREL_HI20,
                                         expr.name,
                                         expr.addend) &&
                   minias_add_relocation(as,
                                         stmt->section,
                                         stmt->offset + 4U,
                                         MINIAS_R_RISCV_PCREL_LO12_I,
                                         anchor_name,
                                         0);
        }
    }
    if (strcmp(stmt->op, "sb") == 0 || strcmp(stmt->op, "sh") == 0 ||
        strcmp(stmt->op, "sw") == 0 || strcmp(stmt->op, "sd") == 0) {
        char base[128];

        if (count != 2U || !require_reg(as, stmt, operands[0], &rs2) ||
            !parse_mem(operands[1], &immediate, base) ||
            !require_reg(as, stmt, base, &rs1)) {
            return false;
        }
        funct3 = strcmp(stmt->op, "sb") == 0   ? 0U
                 : strcmp(stmt->op, "sh") == 0 ? 1U
                 : strcmp(stmt->op, "sw") == 0 ? 2U
                                                : 3U;
        return append_u32(as, stmt->section, enc_s(funct3, rs1, rs2, immediate));
    }
    if (strcmp(stmt->op, "jalr") == 0) {
        if (count == 1U) {
            if (!require_reg(as, stmt, operands[0], &rs1)) {
                return false;
            }
            rd = 1;
            immediate = 0;
        } else if (count == 2U) {
            char base[128];

            if (!require_reg(as, stmt, operands[0], &rd) ||
                !parse_mem(operands[1], &immediate, base) ||
                !require_reg(as, stmt, base, &rs1)) {
                return false;
            }
        } else if (count == 3U) {
            if (!require_reg(as, stmt, operands[0], &rd) ||
                !require_reg(as, stmt, operands[1], &rs1) ||
                !require_imm(as, stmt, operands[2], &immediate)) {
                return false;
            }
        } else {
            minias_set_error(as, "operand-count:jalr:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as,
                          stmt->section,
                          enc_i(0x67U, rd, 0U, rs1, immediate));
    }
    if (strcmp(stmt->op, "j") == 0 || strcmp(stmt->op, "jal") == 0) {
        MiniAsSymbolExpr expr;
        const char *target_text;

        if (strcmp(stmt->op, "j") == 0) {
            if (count != 1U) {
                minias_set_error(as, "operand-count:j:line=%zu", stmt->line);
                return false;
            }
            rd = 0;
            target_text = operands[0];
        } else if (count == 1U) {
            rd = 1;
            target_text = operands[0];
        } else if (count == 2U) {
            if (!require_reg(as, stmt, operands[0], &rd)) {
                return false;
            }
            target_text = operands[1];
        } else {
            minias_set_error(as, "operand-count:jal:line=%zu", stmt->line);
            return false;
        }

        if (!minias_parse_symbol_addend(target_text, &expr)) {
            minias_set_error(as,
                             "unsupported-expression:%s:%s:line=%zu",
                             stmt->op,
                             target_text,
                             stmt->line);
            return false;
        }

        symbol = minias_get_symbol(as, expr.name, false);
        if (symbol != NULL && symbol->defined && symbol->section == stmt->section &&
            symbol->subsection == stmt->subsection) {
            immediate = (int64_t)symbol->value + expr.addend -
                        (int64_t)stmt->offset;
            if ((immediate & 1) != 0 || immediate < -(1LL << 20) ||
                immediate > (1LL << 20) - 2) {
                minias_set_error(as, "jal-range:line=%zu", stmt->line);
                return false;
            }
            return append_u32(as, stmt->section, enc_j(rd, immediate));
        }

        return append_u32(as, stmt->section, enc_j(rd, 0)) &&
               minias_add_relocation(as,
                                     stmt->section,
                                     stmt->offset,
                                     MINIAS_R_RISCV_JAL,
                                     expr.name,
                                     expr.addend);
    }
    if (strcmp(stmt->op, "beq") == 0 || strcmp(stmt->op, "bne") == 0 ||
        strcmp(stmt->op, "blt") == 0 || strcmp(stmt->op, "bge") == 0 ||
        strcmp(stmt->op, "bltu") == 0 || strcmp(stmt->op, "bgeu") == 0 ||
        strcmp(stmt->op, "beqz") == 0 || strcmp(stmt->op, "bnez") == 0 ||
        strcmp(stmt->op, "bltz") == 0 || strcmp(stmt->op, "bgez") == 0 ||
        strcmp(stmt->op, "bgtz") == 0 || strcmp(stmt->op, "blez") == 0 ||
        strcmp(stmt->op, "bgt") == 0 || strcmp(stmt->op, "ble") == 0 ||
        strcmp(stmt->op, "bgtu") == 0 || strcmp(stmt->op, "bleu") == 0) {
        const char *target;

        if (strcmp(stmt->op, "beqz") == 0 || strcmp(stmt->op, "bnez") == 0 ||
            strcmp(stmt->op, "bltz") == 0 || strcmp(stmt->op, "bgez") == 0 ||
            strcmp(stmt->op, "bgtz") == 0 || strcmp(stmt->op, "blez") == 0) {
            int operand_reg;

            if (count != 2U || !require_reg(as, stmt, operands[0], &operand_reg)) {
                return false;
            }
            target = operands[1];
            if (strcmp(stmt->op, "beqz") == 0) {
                rs1 = operand_reg;
                rs2 = 0;
                funct3 = 0U;
            } else if (strcmp(stmt->op, "bnez") == 0) {
                rs1 = operand_reg;
                rs2 = 0;
                funct3 = 1U;
            } else if (strcmp(stmt->op, "bltz") == 0) {
                rs1 = operand_reg;
                rs2 = 0;
                funct3 = 4U;
            } else if (strcmp(stmt->op, "bgez") == 0) {
                rs1 = operand_reg;
                rs2 = 0;
                funct3 = 5U;
            } else if (strcmp(stmt->op, "bgtz") == 0) {
                rs1 = 0;
                rs2 = operand_reg;
                funct3 = 4U;
            } else {
                rs1 = 0;
                rs2 = operand_reg;
                funct3 = 5U;
            }
        } else {
            int first;
            int second;

            if (count != 3U || !require_reg(as, stmt, operands[0], &first) ||
                !require_reg(as, stmt, operands[1], &second)) {
                return false;
            }
            target = operands[2];
            if (strcmp(stmt->op, "bgt") == 0 || strcmp(stmt->op, "ble") == 0 ||
                strcmp(stmt->op, "bgtu") == 0 || strcmp(stmt->op, "bleu") == 0) {
                rs1 = second;
                rs2 = first;
                funct3 = strcmp(stmt->op, "bgt") == 0    ? 4U
                         : strcmp(stmt->op, "ble") == 0   ? 5U
                         : strcmp(stmt->op, "bgtu") == 0  ? 6U
                                                          : 7U;
            } else {
                rs1 = first;
                rs2 = second;
                funct3 = strcmp(stmt->op, "bne") == 0    ? 1U
                         : strcmp(stmt->op, "blt") == 0  ? 4U
                         : strcmp(stmt->op, "bge") == 0  ? 5U
                         : strcmp(stmt->op, "bltu") == 0 ? 6U
                         : strcmp(stmt->op, "bgeu") == 0 ? 7U
                                                          : 0U;
            }
        }
        symbol = minias_get_symbol(as, target, false);
        if (symbol == NULL || !symbol->defined || symbol->section != stmt->section) {
            minias_set_error(as,
                             "unsupported-relocation:%s:line=%zu",
                             stmt->op,
                             stmt->line);
            return false;
        }
        immediate = (int64_t)symbol->value - (int64_t)stmt->offset;
        if ((immediate & 1) != 0 || immediate < -4096 || immediate > 4094) {
            minias_set_error(as, "branch-range:%s:line=%zu", stmt->op, stmt->line);
            return false;
        }
        return append_u32(as, stmt->section, enc_b(funct3, rs1, rs2, immediate));
    }

    minias_set_error(as, "unsupported-instruction:%s:line=%zu", stmt->op, stmt->line);
    return false;
}
