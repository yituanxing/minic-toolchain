#include "minias_internal.h"

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

static bool parse_i64(const char *text, int64_t *value) {
    char *end = NULL;
    long long parsed;

    errno = 0;
    parsed = strtoll(text, &end, 0);
    if (errno != 0 || end == text) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    *value = (int64_t)parsed;
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
        if (errno != 0 || end == text) {
            return false;
        }
        while (*end == ' ' || *end == '\t') {
            ++end;
        }
        if (*end != '\0') {
            return false;
        }
        *signed_value = (int64_t)parsed;
        *bits = (uint64_t)*signed_value;
        *fits_signed32 =
            *signed_value >= INT32_MIN && *signed_value <= INT32_MAX;
        return true;
    }
    {
        unsigned long long parsed;
        errno = 0;
        parsed = strtoull(text, &end, 0);
        if (errno != 0 || end == text) {
            return false;
        }
        while (*end == ' ' || *end == '\t') {
            ++end;
        }
        if (*end != '\0') {
            return false;
        }
        *bits = (uint64_t)parsed;
        if (*bits <= (uint64_t)INT64_MAX) {
            *signed_value = (int64_t)*bits;
            *fits_signed32 = *signed_value <= INT32_MAX;
        } else {
            *signed_value = 0;
            *fits_signed32 = false;
        }
        return true;
    }
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
    char *end = NULL;
    unsigned long value;

    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(text, names[i].name) == 0) {
            *csr = names[i].value;
            return true;
        }
    }
    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || value > 0xfffUL) {
        return false;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    *csr = (uint32_t)value;
    return true;
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

static bool parse_mem(const char *text, int64_t *offset, char base[128]) {
    const char *left = strchr(text, '(');
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

bool minias_riscv_measure(const char *op,
                          const char *args,
                          uint32_t *size,
                          char *reason,
                          size_t reason_size) {
    char operands[4][128];
    size_t count = split_operands(args, operands, 4U);

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
        if (count != 1U || !minias_parse_symbol_addend(operands[0], &expr)) {
            (void)snprintf(reason, reason_size, "unsupported-expression:%s", args);
            return false;
        }
        *size = 8U;
        return true;
    }
    if (strcmp(op, "tail") == 0) {
        (void)snprintf(reason, reason_size, "unsupported-reloc-instruction:%s", op);
        return false;
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
    if (SIMPLE("ret") || SIMPLE("nop") || SIMPLE("mv") || SIMPLE("jr") ||
        SIMPLE("snez") || SIMPLE("seqz") || SIMPLE("neg") || SIMPLE("jalr") ||
        SIMPLE("j") || SIMPLE("beq") || SIMPLE("bne") ||
        SIMPLE("blt") || SIMPLE("bge") || SIMPLE("bltu") || SIMPLE("bgeu") ||
        SIMPLE("beqz") || SIMPLE("bnez") || SIMPLE("bltz") || SIMPLE("bgez") ||
        SIMPLE("bgtz") || SIMPLE("blez") || SIMPLE("bgt") || SIMPLE("ble") ||
        SIMPLE("bgtu") || SIMPLE("bleu") || SIMPLE("addi") || SIMPLE("addiw") ||
        SIMPLE("andi") || SIMPLE("ori") || SIMPLE("xori") || SIMPLE("slti") ||
        SIMPLE("sltiu") || SIMPLE("slli") || SIMPLE("srli") || SIMPLE("srai") ||
        SIMPLE("sra") || SIMPLE("fence") || SIMPLE("csrr") || SIMPLE("csrrc") ||
        SIMPLE("csrs") || SIMPLE("csrc") || SIMPLE("ebreak") || SIMPLE("pause") ||
        SIMPLE("wfi") || SIMPLE("add") || SIMPLE("sub") || SIMPLE("sll") ||
        SIMPLE("srl") || SIMPLE("and") || SIMPLE("or") ||
        SIMPLE("xor") || SIMPLE("slt") || SIMPLE("sltu") || SIMPLE("mul") ||
        SIMPLE("mulh") || SIMPLE("mulhu") || SIMPLE("div") || SIMPLE("divu") ||
        SIMPLE("rem") || SIMPLE("remu") || SIMPLE("lb") || SIMPLE("lbu") ||
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
    char operands[4][128];
    size_t count = split_operands(stmt->args, operands, 4U);
    int rd;
    int rs1;
    int rs2;
    int64_t immediate;
    uint32_t value = 0U;
    uint32_t funct3;
    MiniAsSymbol *symbol;

    if (strcmp(stmt->op, "ret") == 0) {
        return append_u32(as, stmt->section, enc_i(0x67U, 0, 0U, 1, 0));
    }
    if (strcmp(stmt->op, "nop") == 0) {
        return append_u32(as, stmt->section, enc_i(0x13U, 0, 0U, 0, 0));
    }
    if (strcmp(stmt->op, "mv") == 0) {
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

        if (count != 1U || !minias_parse_symbol_addend(operands[0], &expr)) {
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
            !require_reg(as, stmt, operands[1], &rs1) ||
            !require_reg(as, stmt, operands[2], &rs2)) {
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

        if (count != 2U || !require_reg(as, stmt, operands[0], &rd) ||
            !parse_mem(operands[1], &immediate, base) ||
            !require_reg(as, stmt, base, &rs1)) {
            return false;
        }
        funct3 = strcmp(stmt->op, "lb") == 0    ? 0U
                 : strcmp(stmt->op, "lh") == 0  ? 1U
                 : strcmp(stmt->op, "lw") == 0  ? 2U
                 : strcmp(stmt->op, "ld") == 0  ? 3U
                 : strcmp(stmt->op, "lbu") == 0 ? 4U
                 : strcmp(stmt->op, "lhu") == 0 ? 5U
                                                 : 6U;
        return append_u32(as, stmt->section, enc_i(0x03U, rd, funct3, rs1, immediate));
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
        if (count != 3U || !require_reg(as, stmt, operands[0], &rd) ||
            !require_reg(as, stmt, operands[1], &rs1) ||
            !require_imm(as, stmt, operands[2], &immediate)) {
            return false;
        }
        return append_u32(as, stmt->section, enc_i(0x67U, rd, 0U, rs1, immediate));
    }
    if (strcmp(stmt->op, "j") == 0) {
        if (count != 1U) {
            minias_set_error(as, "operand-count:j:line=%zu", stmt->line);
            return false;
        }
        symbol = minias_get_symbol(as, operands[0], false);
        if (symbol == NULL || !symbol->defined || symbol->section != stmt->section) {
            minias_set_error(as, "unsupported-relocation:j:line=%zu", stmt->line);
            return false;
        }
        immediate = (int64_t)symbol->value - (int64_t)stmt->offset;
        if ((immediate & 1) != 0 || immediate < -(1LL << 20) ||
            immediate > (1LL << 20) - 2) {
            minias_set_error(as, "jal-range:line=%zu", stmt->line);
            return false;
        }
        return append_u32(as, stmt->section, enc_j(0, immediate));
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
