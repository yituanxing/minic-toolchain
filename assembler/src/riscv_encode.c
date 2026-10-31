#include "minias_internal.h"
#include "riscv_fixup_kinds.h"
#include "riscv_profile.h"
#include "riscv_target.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* These are retained RISC-V pseudo forms, not instruction word templates.
 * MCInst.encoding is reserved for actual target encoding bits. */
enum {
    RV_PSEUDO_LI_WIDE = 1U,
    RV_PSEUDO_LI_SIGNED32,
    RV_PSEUDO_LLA,
    RV_PSEUDO_LA
};

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

static bool parse_csr_numeric_expression(const char *text, uint32_t *csr) {
    uint64_t value;
    /* CSR immediates use the same constant-expression grammar as other
     * integer operands. The target boundary only checks the 12-bit range;
     * no independent arithmetic parser or overflow rules live here. */
    if (!minias_eval_u64_expression(text, &value) || value > 0xfffU)
        return false;
    *csr = (uint32_t)value;
    return true;
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


/* AMO and LR/SC share one suffix grammar and one set of ordering bits.
 * The caller only selects the operation's funct5/operand layout. */
static bool rv_decode_atomic_suffix(const char *op, char *core, size_t cap,
                                    uint32_t *width_funct3,
                                    uint32_t *ordering_bits) {
    size_t n;
    if (op == NULL || core == NULL || width_funct3 == NULL ||
        ordering_bits == NULL) return false;
    n = strlen(op);
    if (n == 0U || n >= cap) return false;
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
    if (n < 3U || core[n - 2U] != '.' ||
        (core[n - 1U] != 'w' && core[n - 1U] != 'd'))
        return false;
    *width_funct3 = core[n - 1U] == 'w' ? 2U : 3U;
    core[n - 2U] = '\0';
    return true;
}

static bool decode_amo_mnemonic(const char *op,
                                uint32_t *funct5,
                                uint32_t *width_funct3,
                                uint32_t *ordering_bits) {
    static const struct {
        const char *name;
        uint32_t funct5;
    } forms[] = {
        {"amoadd", 0x00U}, {"amoswap", 0x01U},
        {"amoxor", 0x04U}, {"amoor", 0x08U},
        {"amoand", 0x0cU}, {"amomin", 0x10U},
        {"amomax", 0x14U}, {"amominu", 0x18U},
        {"amomaxu", 0x1cU}
    };
    char core[64];
    size_t i;
    if (funct5 == NULL ||
        !rv_decode_atomic_suffix(op,core,sizeof(core),
                                 width_funct3,ordering_bits))return false;
    for(i=0U;i<sizeof(forms)/sizeof(forms[0]);++i) {
        if(strcmp(core,forms[i].name)!=0)continue;
        *funct5=forms[i].funct5;
        return true;
    }
    return false;
}

static bool decode_lr_sc_mnemonic(const char *op,
                                  bool *is_lr,
                                  uint32_t *width_funct3,
                                  uint32_t *ordering_bits) {
    char core[32];
    if (is_lr == NULL ||
        !rv_decode_atomic_suffix(op,core,sizeof(core),
                                 width_funct3,ordering_bits))return false;

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

static bool split_operands(const char *args, char out[][128],
                           size_t max_out, size_t *count) {
    char *slots[8];
    size_t capacities[8],i;
    unsigned parsed=0U;
    if(count==NULL || max_out==0U || max_out>8U)return false;
    *count=0U;
    memset(out,0,max_out*sizeof(out[0]));
    if(args==NULL || *args=='\0')return true;
    for(i=0U;i<max_out;++i) {
        slots[i]=out[i];
        capacities[i]=sizeof(out[i]);
    }
    if(!minias_split_mc_operands(args,slots,capacities,
                                  (unsigned)max_out,&parsed))return false;
    *count=parsed;
    return true;
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

/* A single RV LI.D/LI.W materialization plan supplies BOTH retained-MCInst
 * measurement and emitted machine bytes. Previously their independent
 * 32/64-bit heuristic branches could silently disagree when a new encoding
 * alternative changed the number of words after layout. At most 15 words:
 * leading ADDI + seven shifts and seven nonzero-byte ADDIs. */
static bool rv_materialize_li(MiniAs *as, const MiniAsStmt *stmt,
                              uint32_t words[16], unsigned *count) {
    const MiniAsMCInst *m = stmt->mcinst;
    const uint64_t bits = m->operands[1].immediate;
    const int64_t signed_value = (int64_t)m->operands[2].immediate;
    const bool fits_signed32 = m->target_form == RV_PSEUDO_LI_SIGNED32;
    const int rd = (int)m->operands[0].reg;
    int64_t hi, lo;
    *count = 0U;
    if (m->target_form != RV_PSEUDO_LI_SIGNED32 &&
        m->target_form != RV_PSEUDO_LI_WIDE) {
        minias_set_error(as, "internal:riscv-li-invalid-form");
        return false;
    }
    if (MINIAS_RV_STATE(as)->target_xlen == 32U) {
        int64_t value;
        if (!rv32_li_value(bits, &value)) {
            minias_set_error(as, "rv32i-li-overflows-32-bits:line=%zu",
                             stmt->line);
            return false;
        }
        if (value >= -2048 && value <= 2047) {
            words[(*count)++] = enc_i(0x13U, rd, 0U, 0, value);
            return true;
        }
        hi = (value + INT64_C(2048)) >> 12;
        lo = value - hi * INT64_C(4096);
        words[(*count)++] = 0x37U | ((uint32_t)rd << 7U) |
                            (((uint32_t)hi & UINT32_C(0xfffff)) << 12U);
        if (lo != 0) words[(*count)++] = enc_i(0x13U, rd, 0U, rd, lo);
        return true;
    }
    if (fits_signed32) {
        if (signed_value >= -2048 && signed_value <= 2047) {
            words[(*count)++] = enc_i(0x13U, rd, 0U, 0, signed_value);
            return true;
        }
        hi = (signed_value + INT64_C(2048)) >> 12;
        lo = signed_value - hi * INT64_C(4096);
        words[(*count)++] = 0x37U | ((uint32_t)rd << 7U) |
                            (((uint32_t)hi & UINT32_C(0xfffff)) << 12U);
        words[(*count)++] = enc_i(0x1bU, rd, 0U, rd, lo);
        return true;
    }
    {
        int index = 7, i;
        uint64_t top;
        while (index > 0 &&
               ((bits >> ((unsigned)index * 8U)) & UINT64_C(0xff)) == 0U)
            --index;
        top = (bits >> ((unsigned)index * 8U)) & UINT64_C(0xff);
        words[(*count)++] = enc_i(0x13U, rd, 0U, 0, (int64_t)top);
        for (i = index - 1; i >= 0; --i) {
            uint64_t byte = (bits >> ((unsigned)i * 8U)) & UINT64_C(0xff);
            words[(*count)++] = enc_i(0x13U, rd, 1U, rd, 8);
            if (byte != 0U)
                words[(*count)++] = enc_i(0x13U, rd, 0U, rd, (int64_t)byte);
        }
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

/* GCC's non-PIC absolute-address sequence is two independently relocated
 * instructions (LUI %hi and an I/S-form %lo), not a PC-relative LA pseudo.
 * Preserve its symbol/addend in an owned MC operand until final emission. */
static bool parse_absolute_modifier(const char *text, const char *modifier,
                                    MiniAsSymbolExpr *expr) {
    size_t prefix, length;
    char inner[128];
    if (text == NULL || modifier == NULL || expr == NULL) return false;
    prefix = strlen(modifier);
    length = strlen(text);
    if (length <= prefix + 1U ||
        strncmp(text, modifier, prefix) != 0 ||
        text[length - 1U] != ')' ||
        length - prefix - 1U >= sizeof(inner)) return false;
    memcpy(inner, text + prefix, length - prefix - 1U);
    inner[length - prefix - 1U] = '\0';
    return minias_parse_symbol_addend(inner, expr);
}

static bool parse_absolute_lo_memory(const char *text,
                                     MiniAsSymbolExpr *expr,
                                     char base[128]) {
    const char *after_expr, *end;
    size_t prefix_length, register_length;
    char modifier[128];
    if (text == NULL || strncmp(text, "%lo(", 4U) != 0) return false;
    after_expr = strchr(text + 4U, ')');
    if (after_expr == NULL || after_expr[1] != '(') return false;
    end = strchr(after_expr + 2U, ')');
    if (end == NULL || end[1] != '\0') return false;
    prefix_length = (size_t)(after_expr - text + 1);
    register_length = (size_t)(end - (after_expr + 2U));
    if (prefix_length >= sizeof(modifier) ||
        register_length == 0U || register_length >= 128U) return false;
    memcpy(modifier, text, prefix_length);
    modifier[prefix_length] = '\0';
    memcpy(base, after_expr + 2U, register_length);
    base[register_length] = '\0';
    return parse_absolute_modifier(modifier, "%lo(", expr);
}

enum {
    RV_ABS_RELOC_NONE = 0U,
    RV_ABS_RELOC_HI20,
    RV_ABS_RELOC_LO12_I,
    RV_ABS_RELOC_LO12_S
};

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
    if(!split_operands(p,operands,8U,&count))return false;

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

static bool parse_call_target(const char *text, MiniAsSymbolExpr *expr,
                              MiniAsRiscvSymbolModifier *modifier) {
    char normalized[256];
    char *plt;
    char *suffix;
    size_t length;

    if (text == NULL || expr == NULL || modifier == NULL) {
        return false;
    }
    *modifier = MINIAS_RV_SYM_NONE;
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
        *modifier = MINIAS_RV_SYM_PLT;
    }
    return minias_parse_symbol_addend(normalized, expr);
}

/* Retain only a stable parse-time SymbolNameId and addend in MCInst. Common
 * does not create a real ELF Symbol until a definition or Fixup commit; this
 * preserves ELF encounter order while layout can resolve current values. */
static bool rv_bind_symbol_operand(MiniAs *as, MiniAsMCInst *m,
                                   unsigned index,
                                   const MiniAsSymbolExpr *parsed) {
    MiniAsSymbolRef *ref;
    if (index >= m->capacity) {
        minias_set_error(as, "internal:riscv-symbol-operand-index");
        return false;
    }
    ref = malloc(sizeof(*ref));
    if (ref == NULL) {
        minias_set_error(as, "out-of-memory:riscv-bound-symbol");
        return false;
    }
    if (!minias_bind_symbol_ref(as, parsed, ref)) {
        free(ref);
        return false;
    }
    m->operands[index].bound_symbol = ref;
    m->operands[index].kind = MINIAS_MC_BOUND_SYMBOL;
    return true;
}

/* A single canonical 32-bit word builder serves RVC selection and emission.
 * RVC rejects relocation-bearing forms because their I/U fields have a
 * different psABI contract; ELF emission retains the zero-immediate word
 * placeholder and attaches the typed Fixup afterward. */
static bool rv_scalar_machine_word(const MiniAsMCInst *m,
                                   bool allow_reloc_placeholder,
                                   uint32_t *word) {
    uint32_t base=m->encoding;
    int rd,rs1=0,rs2=0,rs3=0;
    int64_t imm=0;
    if (m->opcode == RV_MC_LI) {
        const MiniAsRiscvLiPlan *plan=m->target_payload;
        if (plan==NULL || plan->count!=1U) return false;
        *word=plan->words[0];
        return true;
    }
    if ((!allow_reloc_placeholder && m->target_form!=RV_ABS_RELOC_NONE) ||
        m->count==0U) return false;
    rd=(int)m->operands[0].reg;
    if(m->count>1U && m->operands[1].kind==MINIAS_MC_REGISTER)
        rs1=(int)m->operands[1].reg;
    else if(m->opcode==RV_MC_WORD && m->count>1U &&
            m->operands[1].kind==MINIAS_MC_IMMEDIATE)
        rs1=(int)m->operands[1].immediate;
    if(m->count>2U && m->operands[2].kind==MINIAS_MC_REGISTER)
        rs2=(int)m->operands[2].reg;
    if(m->count>2U && m->operands[2].kind==MINIAS_MC_IMMEDIATE)
        imm=(int64_t)m->operands[2].immediate;
    if(m->count>3U && m->operands[3].kind==MINIAS_MC_REGISTER)
        rs3=(int)m->operands[3].reg;
    switch(m->opcode) {
    case RV_MC_WORD:
        *word=base|((uint32_t)rd<<7U)|((uint32_t)rs1<<15U)|
              ((uint32_t)rs2<<20U)|((uint32_t)rs3<<27U);
        return true;
    case RV_MC_R:
        *word=enc_r(base&0x7fU,rd,(base>>12U)&7U,rs1,rs2,base>>25U);
        return true;
    case RV_MC_I:
    case RV_MC_LOAD:
        if(m->opcode==RV_MC_I && m->count==2U)
            imm=(int64_t)m->operands[1].immediate;
        *word=base|((uint32_t)rd<<7U)|((uint32_t)rs1<<15U)|
              (((uint32_t)imm&0xfffU)<<20U);
        return true;
    case RV_MC_STORE: {
        uint32_t bits=(uint32_t)imm&0xfffU;
        *word=base|((uint32_t)rd<<20U)|((uint32_t)rs1<<15U)|
              ((bits&31U)<<7U)|(((bits>>5U)&127U)<<25U);
        return true;
    }
    case RV_MC_U:
        *word=base|((uint32_t)rd<<7U)|
              (((uint32_t)m->operands[1].immediate&0xfffffU)<<12U);
        return true;
    default:
        return false;
    }
}

static bool rv_rvc_encode_plain(const MiniAs *as, const MiniAsStmt *stmt,
                                 uint16_t *compressed) {
    const MiniAsRiscvState *state=MINIAS_RV_STATE(as);
    const MiniAsMCInst *m=stmt->mcinst;
    uint32_t w,op,rd,rs1,rs2,f3,imm12;
    int32_t imm;
    uint16_t out;
    if(state==NULL || !state->isa_explicit ||
       (state->isa_features&RISCV_ISA_C)==0U ||
       (stmt->site.mode_bits&MINIAS_RISCV_OPTION_NO_RVC)!=0U ||
       m==NULL || !rv_scalar_machine_word(m,false,&w))return false;
    op=w&127U;
    rd=(w>>7U)&31U;
    f3=(w>>12U)&7U;
    rs1=(w>>15U)&31U;
    rs2=(w>>20U)&31U;
    imm12=(w>>20U)&4095U;
    imm=(int32_t)((imm12^2048U)-2048U);
    out=0U;
    if(op==0x13U && f3==0U) { /* ADDI, LI, MV and ADDI16SP */
        if(rd==0U && rs1==0U && imm==0)out=0x0001U;
        else if(rd!=0U && rs1==0U && imm>=-32 && imm<=31)
            out=(uint16_t)(0x4001U|(rd<<7U)|
                  (((uint32_t)imm&32U)<<7U)|(((uint32_t)imm&31U)<<2U));
        else if(rd!=0U && rs1==rd && imm!=0 && imm>=-32 && imm<=31)
            out=(uint16_t)(0x0001U|(rd<<7U)|
                  (((uint32_t)imm&32U)<<7U)|(((uint32_t)imm&31U)<<2U));
        else if(rd==2U && rs1==2U && imm!=0 && imm>=-512 &&
                imm<=496 && (imm&15)==0)
            out=(uint16_t)(0x6101U|
                  (((uint32_t)imm>>9U&1U)<<12U)|
                  (((uint32_t)imm>>4U&1U)<<6U)|
                  (((uint32_t)imm>>6U&1U)<<5U)|
                  (((uint32_t)imm>>7U&3U)<<3U)|
                  (((uint32_t)imm>>5U&1U)<<2U));
        else if(rd!=0U && rs1!=0U && rd!=rs1 && imm==0)
            out=(uint16_t)(0x8002U|(rd<<7U)|(rs1<<2U));
    } else if(op==0x1bU && f3==0U && rd==rs1 && rd!=0U &&
              imm>=-32 && imm<=31)
        out=(uint16_t)(0x2001U|(rd<<7U)|
              (((uint32_t)imm&32U)<<7U)|(((uint32_t)imm&31U)<<2U));
    else if(op==0x67U && f3==0U && imm==0 && rs1!=0U &&
            (rd==0U || rd==1U))
        out=(uint16_t)((rd==0U?0x8002U:0x9002U)|(rs1<<7U));
    else if(op==0x3bU && (f3==0U) && rd>=8U && rd<=15U &&
            rd==rs1 && rs2>=8U && rs2<=15U &&
            ((w>>25U)==0U || (w>>25U)==32U))
        out=(uint16_t)(((w>>25U)==0U?0x9c21U:0x9c01U)|
                       ((rd-8U)<<7U)|((rs2-8U)<<2U));
    else if((op==0x03U && (f3==2U || f3==3U)) ||
            (op==0x23U && (f3==2U || f3==3U))) {
        bool load=op==0x03U;
        bool word=f3==2U;
        uint32_t reg=load?rd:rs2;
        uint32_t uimm=load?imm12:
            (((w>>25U)&127U)<<5U)|((w>>7U)&31U);
        if(uimm>2047U || (uimm&(word?3U:7U))!=0U) return false;
        if(rs1==2U && (!load || reg!=0U)) {
            if(word && uimm<=252U) {
                if(load)out=(uint16_t)(0x4002U|(reg<<7U)|
                    (((uimm>>5U)&1U)<<12U)|(((uimm>>2U)&7U)<<4U)|
                    (((uimm>>6U)&3U)<<2U));
                else out=(uint16_t)(0xc002U|(reg<<2U)|
                    (((uimm>>2U)&15U)<<9U)|(((uimm>>6U)&3U)<<7U));
            } else if(!word && uimm<=504U) {
                if(load)out=(uint16_t)(0x6002U|(reg<<7U)|
                    (((uimm>>5U)&1U)<<12U)|(((uimm>>3U)&3U)<<5U)|
                    (((uimm>>6U)&7U)<<2U));
                else out=(uint16_t)(0xe002U|(reg<<2U)|
                    (((uimm>>3U)&7U)<<10U)|(((uimm>>6U)&7U)<<7U));
            }
        } else if(rs1>=8U && rs1<=15U && reg>=8U && reg<=15U) {
            uint16_t prefix=(uint16_t)((load?(word?0x4000U:0x6000U):
                                             (word?0xc000U:0xe000U))|
                                        ((rs1-8U)<<7U)|((reg-8U)<<2U));
            if(word && uimm<=124U)
                out=(uint16_t)(prefix|(((uimm>>3U)&7U)<<10U)|
                                      (((uimm>>2U)&1U)<<6U)|
                                      (((uimm>>6U)&1U)<<5U));
            else if(!word && uimm<=248U)
                out=(uint16_t)(prefix|(((uimm>>3U)&7U)<<10U)|
                                      (((uimm>>6U)&3U)<<5U));
        }
    }
    if(out==0U)return false;
    *compressed=out;
    return true;
}

/* Branch width is initially selected from the ISA/register form; MCLayout
 * may grow a short form if its eventual target is out of C range. */
static bool rv_rvc_branch_form(const MiniAs *as,const MiniAsStmt *stmt,
                                bool *jump) {
    const MiniAsRiscvState *state=MINIAS_RV_STATE(as);
    const MiniAsMCInst *m=stmt->mcinst;
    if(state==NULL || !state->isa_explicit ||
       (state->isa_features&RISCV_ISA_C)==0U ||
       (stmt->site.mode_bits&MINIAS_RISCV_OPTION_NO_RVC)!=0U ||
       m==NULL)return false;
    if(m->opcode==RV_MC_JAL && m->operands[0].reg==0U) {
        *jump=true;
        return true;
    }
    if(m->opcode==RV_MC_BRANCH &&
       ((m->encoding>>12U)&7U)<=1U &&
       m->operands[0].reg>=8U && m->operands[0].reg<=15U &&
       m->operands[1].reg==0U) {
        *jump=false;
        return true;
    }
    return false;
}

static uint16_t rv_rvc_branch_bits(int64_t d,unsigned rs1,bool neq) {
    uint32_t bits=(uint32_t)d;
    return (uint16_t)((neq?0xe001U:0xc001U)|
           ((rs1-8U)<<7U)|(((bits>>8U)&1U)<<12U)|
           (((bits>>3U)&3U)<<10U)|(((bits>>6U)&3U)<<5U)|
           (((bits>>1U)&3U)<<3U)|(((bits>>5U)&1U)<<2U));
}

static uint16_t rv_rvc_jump_bits(int64_t d) {
    uint32_t bits=(uint32_t)d;
    return (uint16_t)(0xa001U|(((bits>>11U)&1U)<<12U)|
           (((bits>>4U)&1U)<<11U)|(((bits>>8U)&3U)<<9U)|
           (((bits>>10U)&1U)<<8U)|(((bits>>6U)&1U)<<7U)|
           (((bits>>7U)&1U)<<6U)|(((bits>>1U)&7U)<<3U)|
           (((bits>>5U)&1U)<<2U));
}

/* Measure the actual persisted target instruction. The layout driver
 * invokes this once after operand parsing and before placement. Neither
 * symbol spelling nor source-level expression grammar is revisited. */
bool minias_riscv_measure_mcinst(MiniAs *as, const MiniAsStmt *stmt,
                                 uint32_t *size) {
    const MiniAsMCInst *m = stmt->mcinst;
    uint16_t compressed;
    bool compressed_jump;
    if (m == NULL) {
        minias_set_error(as, "unsupported-instruction:%s:line=%zu",
                         stmt->op, stmt->line);
        return false;
    }
    if(rv_rvc_encode_plain(as,stmt,&compressed)) {
        *size=2U;
        return true;
    }
    if(rv_rvc_branch_form(as,stmt,&compressed_jump)) {
        *size=2U;
        return true;
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
            const MiniAsRiscvLiPlan *plan = m->target_payload;
            if (plan == NULL) {
                minias_set_error(as, "internal:riscv-li-plan-missing:line=%zu",
                                 stmt->line);
                return false;
            }
            *size = plan->count * 4U;
            return true;
        }
        default:
            *size = 4U;
            return true;
    }
}

/* Fixed-width instruction syntax is owned by this one Target descriptor,
 * rather than rediscovered from mnemonic spellings during operand parsing.
 * These bits affect only RISC-V operand lowering, not Common MC semantics. */
typedef enum RvMCFormFlags {
    RV_FORM_DEFAULT=0U,
    RV_FORM_TWO_OPERANDS=1U<<0,
    RV_FORM_FIXED_IMMEDIATE=1U<<1,
    RV_FORM_ALT_IMMEDIATE=1U<<2,
    RV_FORM_NEGATE_IMMEDIATE=1U<<3,
    RV_FORM_ARITH_IMMEDIATE=1U<<4,
    RV_FORM_SHIFT_IMMEDIATE=1U<<5,
    RV_FORM_SWAP_SOURCES=1U<<6,
    RV_FORM_BRANCH_ZERO=1U<<7,
    RV_FORM_BRANCH_ZERO_RS1=1U<<8,
    RV_FORM_BRANCH_REVERSE=1U<<9,
    RV_FORM_JAL_ZERO_RD=1U<<10,
    RV_FORM_REV8_XLEN=1U<<11,
    RV_FORM_HI_RELOCATION=1U<<12
} RvMCFormFlags;

/* RISC-V's fixed-width integer ALU, memory and control transfer forms.
 * Encodings and parser form policy are one Target-owned table; feature and
 * XLEN restrictions remain independent of the source spelling. */
typedef struct RvMCDesc {
    const char *name;
    unsigned family;
    uint32_t base;
    uint64_t required_features;
    bool rv64_only;
    unsigned form_flags;
} RvMCDesc;
#define RVD(name, group, base) {name, RV_MC_##group, base, 0U, false, RV_FORM_DEFAULT}
#define RVDF(name, group, base, flags) {name, RV_MC_##group, base, 0U, false, flags}
#define RVM(name, group, base) {name, RV_MC_##group, base, RISCV_ISA_M, false, RV_FORM_DEFAULT}
#define RVM64(name, group, base) {name, RV_MC_##group, base, RISCV_ISA_M, true, RV_FORM_DEFAULT}
#define RV64(name, group, base) {name, RV_MC_##group, base, 0U, true, RV_FORM_DEFAULT}
#define RV64F(name, group, base, flags) {name, RV_MC_##group, base, 0U, true, flags}
#define RVZF(name, group, base, flags) {name, RV_MC_##group, base, RISCV_ISA_ZBB, false, flags}
#define RV_ALT RV_FORM_ALT_IMMEDIATE
#define RV_SHIFT RV_FORM_SHIFT_IMMEDIATE
#define RV_TWO (RV_FORM_TWO_OPERANDS|RV_FORM_FIXED_IMMEDIATE)
static const RvMCDesc rv_mc_table[] = {
    RVDF("add",R,0x00000033U,RV_ALT),
    RVDF("sub",R,0x40000033U,RV_ALT|RV_FORM_NEGATE_IMMEDIATE),
    RVDF("sll",R,0x00001033U,RV_ALT|RV_SHIFT),
    RVD("slt",R,0x00002033U), RVD("sltu",R,0x00003033U),
    RVDF("xor",R,0x00004033U,RV_ALT),
    RVDF("srl",R,0x00005033U,RV_ALT|RV_SHIFT),
    RVDF("sra",R,0x40005033U,RV_ALT|RV_SHIFT|RV_FORM_ARITH_IMMEDIATE),
    RVDF("or",R,0x00006033U,RV_ALT),
    RVDF("and",R,0x00007033U,RV_ALT),
    RVM("mul",R,0x02000033U), RVM("mulh",R,0x02001033U),
    RVM("mulhu",R,0x02003033U), RVM("div",R,0x02004033U),
    RVM("divu",R,0x02005033U), RVM("rem",R,0x02006033U),
    RVM("remu",R,0x02007033U),
    RV64("addw",R,0x0000003bU), RV64("subw",R,0x4000003bU),
    RV64("sllw",R,0x0000103bU), RV64("srlw",R,0x0000503bU),
    RV64("sraw",R,0x4000503bU),
    RVM64("mulw",R,0x0200003bU), RVM64("divw",R,0x0200403bU),
    RVM64("divuw",R,0x0200503bU), RVM64("remw",R,0x0200603bU),
    RVM64("remuw",R,0x0200703bU),
    RVDF("sgt",R,0x00002033U,RV_FORM_SWAP_SOURCES),
    RVDF("sgtu",R,0x00003033U,RV_FORM_SWAP_SOURCES),
    RVD("addi",I,0x00000013U), RV64("addiw",I,0x0000001bU),
    RVD("slti",I,0x00002013U), RVD("sltiu",I,0x00003013U),
    RVD("xori",I,0x00004013U), RVD("ori",I,0x00006013U),
    RVD("andi",I,0x00007013U),
    RVDF("slli",I,0x00001013U,RV_SHIFT),
    RVDF("srli",I,0x00005013U,RV_SHIFT),
    RVDF("srai",I,0x40005013U,RV_SHIFT),
    RV64F("slliw",I,0x0000101bU,RV_SHIFT),
    RV64F("srliw",I,0x0000501bU,RV_SHIFT),
    RV64F("sraiw",I,0x4000501bU,RV_SHIFT),
    RV64F("sext.w",I,0x0000001bU,RV_TWO),
    RVZF("orc.b",I,0x28705013U,RV_TWO),
    RVZF("rev8",I,0x6b805013U,RV_TWO|RV_FORM_REV8_XLEN),
    RVZF("ctz",I,0x60101013U,RV_TWO),
    RVD("lb",LOAD,0x00000003U), RVD("lh",LOAD,0x00001003U),
    RVD("lw",LOAD,0x00002003U), RV64("ld",LOAD,0x00003003U),
    RVD("lbu",LOAD,0x00004003U), RVD("lhu",LOAD,0x00005003U),
    RV64("lwu",LOAD,0x00006003U),
    RVD("sb",STORE,0x00000023U), RVD("sh",STORE,0x00001023U),
    RVD("sw",STORE,0x00002023U), RV64("sd",STORE,0x00003023U),
    RVDF("lui",U,0x00000037U,RV_FORM_HI_RELOCATION),
    RVD("auipc",U,0x00000017U),
    RVD("beq",BRANCH,0x00000063U), RVD("bne",BRANCH,0x00001063U),
    RVD("blt",BRANCH,0x00004063U), RVD("bge",BRANCH,0x00005063U),
    RVD("bltu",BRANCH,0x00006063U), RVD("bgeu",BRANCH,0x00007063U),
    RVDF("beqz",BRANCH,0x00000063U,RV_FORM_BRANCH_ZERO),
    RVDF("bnez",BRANCH,0x00001063U,RV_FORM_BRANCH_ZERO),
    RVDF("bltz",BRANCH,0x00004063U,RV_FORM_BRANCH_ZERO),
    RVDF("bgez",BRANCH,0x00005063U,RV_FORM_BRANCH_ZERO),
    RVDF("bgtz",BRANCH,0x00004063U,RV_FORM_BRANCH_ZERO|RV_FORM_BRANCH_ZERO_RS1),
    RVDF("blez",BRANCH,0x00005063U,RV_FORM_BRANCH_ZERO|RV_FORM_BRANCH_ZERO_RS1),
    RVDF("bgt",BRANCH,0x00004063U,RV_FORM_BRANCH_REVERSE),
    RVDF("ble",BRANCH,0x00005063U,RV_FORM_BRANCH_REVERSE),
    RVDF("bgtu",BRANCH,0x00006063U,RV_FORM_BRANCH_REVERSE),
    RVDF("bleu",BRANCH,0x00007063U,RV_FORM_BRANCH_REVERSE),
    RVDF("j",JAL,0x0000006fU,RV_FORM_JAL_ZERO_RD),
    RVD("jal",JAL,0x0000006fU)
};
#undef RV_ALT
#undef RV_SHIFT
#undef RV_TWO
#undef RVDF
#undef RVM
#undef RVM64
#undef RV64
#undef RV64F
#undef RVZF
#undef RVD

static const RvMCDesc *rv_find_mc_descriptor(const char *mnemonic) {
    size_t index;
    for (index = 0U; index < sizeof(rv_mc_table) / sizeof(rv_mc_table[0]);
         ++index)
        if (strcmp(mnemonic, rv_mc_table[index].name) == 0)
            return &rv_mc_table[index];
    return NULL;
}

/* All target instruction families now construct retained MCInst operands.
 * Fixed-width descriptors share the same typed parse/measure/encode path;
 * variable-size pseudos use the RISC-V-owned lowering and sizing plan. */
static void rv_set_reg(MiniAsMCOperand *op, int num) {
    op->kind = MINIAS_MC_REGISTER;
    op->reg = (unsigned)num;
}
static void rv_set_imm(MiniAsMCOperand *op, int64_t num) {
    op->kind = MINIAS_MC_IMMEDIATE;
    op->immediate = (uint64_t)num;
}

/* Only this Target knows ISA operand shape. Common owns MC operand lifetime,
 * while the encoder consumes canonical rd/rs1/rs2/symbol fields. */

/* All of these instructions are one machine word. Parse target operands
 * once and retain individual architectural registers in MCInst; preserve
 * CSR numbers, vector vtype, and memory/AMO width in the opcode template.
 * No source string is accessed by the final instruction writer. */
static bool rv_mc_word(MiniAs *as, MiniAsStmt *stmt,
                       uint32_t skeleton, int rd, int rs1, int rs2) {
    MiniAsMCInst *m = minias_mcinst_alloc(3U);
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

/* A dynamic form returns its accepted ISA/XLEN contract from the very parse
 * that selected the machine encoding. The registry supplies the base policy;
 * parsers specialize it for suffix-dependent widths/FP formats. There is no
 * second mnemonic decode after an MCInst has been constructed. */
typedef struct RvParsedIsaPolicy RvParsedIsaPolicy;
typedef bool (*RvFormParser)(MiniAs *, MiniAsStmt *, char [][128], size_t,
                             RvParsedIsaPolicy *);
typedef enum RvSimpleFormKind {
    RV_SIMPLE_NONE,
    RV_SIMPLE_ZERO,
    RV_SIMPLE_TWO_REG,
    RV_SIMPLE_JR,
    RV_SIMPLE_FCSR_READ,
    RV_SIMPLE_FCSR_REG,
    RV_SIMPLE_FCSR_IMMEDIATE
} RvSimpleFormKind;
typedef struct RvParseForm {
    const char *spelling;
    RvFormParser parse;
    bool prefix;
    uint64_t required_features;
    bool rv64_only;
    RvSimpleFormKind simple_kind;
    uint32_t simple_encoding;
    uint16_t compressed_encoding;
    bool source_in_rs2;
} RvParseForm;

struct RvParsedIsaPolicy {
    uint64_t required_features;
    bool rv64_only;
    bool handled;
    const RvParseForm *selected_form;
};

/* Common integer, supervisor, CSR aliases and compressed instructions share
 * typed MCInst ownership.  Multiword, position-dependent pseudos also retain
 * an instruction; the Target supplies their expansion-size plan. */
static bool rv_parse_simple_mc(MiniAs *as, MiniAsStmt *stmt,
                               char ops[][128], size_t n,
                               RvParsedIsaPolicy *policy) {
    const char *op = stmt->op;
    const RvParseForm *selected = policy->selected_form;
    int rd=0,rs1=0,rs2=0;
    int64_t imm=0;
    uint32_t pred=15U,succ=15U;
    char base[128];
    policy->handled = true;

    /* Form lookup has already selected the only spelling/encoding/ISA
     * authority. Do not recognize these aliases again with a second chain
     * of mnemonic tests during operand lowering. */
    if (selected != NULL && selected->simple_kind != RV_SIMPLE_NONE) {
        if (selected->simple_kind >= RV_SIMPLE_FCSR_READ) {
            unsigned csr = selected->simple_encoding;
            if (selected->simple_kind == RV_SIMPLE_FCSR_READ) {
                if (n != 1U || (rd = reg_number(ops[0])) < 0) goto invalid;
                return rv_mc_word(as, stmt, (csr << 20U) | 0x2073U,
                                  rd, 0, 0);
            }
            if (n != 1U && n != 2U) goto invalid;
            if (n == 2U && (rd = reg_number(ops[0])) < 0) goto invalid;
            if (selected->simple_kind == RV_SIMPLE_FCSR_IMMEDIATE) {
                if (!parse_i64(ops[n-1U], &imm) || imm < 0 || imm > 31)
                    goto invalid;
                if (!rv_mc_word(as, stmt, (csr << 20U) | 0x5073U,
                                rd, (int)imm, 0)) return false;
                rv_set_imm(&stmt->mcinst->operands[1], imm);
                return true;
            }
            if ((rs1 = reg_number(ops[n-1U])) < 0) goto invalid;
            return rv_mc_word(as, stmt, (csr << 20U) | 0x1073U,
                              rd, rs1, 0);
        }
        if (selected->simple_kind == RV_SIMPLE_ZERO) {
            if (n != 0U) goto invalid;
            if (selected->compressed_encoding != 0U &&
                MINIAS_RV_STATE(as)->isa_explicit &&
                (MINIAS_RV_STATE(as)->isa_features & RISCV_ISA_C) != 0U &&
                (MINIAS_RV_STATE(as)->options & MINIAS_RISCV_OPTION_NO_RVC) == 0U) {
                MiniAsMCInst *m = minias_mcinst_alloc(0U);
                if (m == NULL) goto oom;
                m->opcode = RV_MC_C16;
                m->encoding = selected->compressed_encoding;
                stmt->mcinst = m;
                return true;
            }
            return rv_mc_word(as, stmt, selected->simple_encoding, 0, 0, 0);
        }
        if (selected->simple_kind == RV_SIMPLE_JR) {
            if (n != 1U || (rs1 = reg_number(ops[0])) < 0) goto invalid;
            return rv_mc_word(as, stmt, selected->simple_encoding, 0, rs1, 0);
        }
        if (n != 2U || (rd = reg_number(ops[0])) < 0 ||
            (rs1 = reg_number(ops[1])) < 0) goto invalid;
        return rv_mc_word(as, stmt, selected->simple_encoding, rd,
                          selected->source_in_rs2 ? 0 : rs1,
                          selected->source_in_rs2 ? rs1 : 0);
    }

    if (strcmp(op, ".insn") == 0) {
        MiniAsMCInst *m;
        uint32_t word;
        if (!parse_raw_insn_directive(stmt->args, &word)) goto invalid;
        m = minias_mcinst_alloc(0U);
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
        m=minias_mcinst_alloc(2U);
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
    policy->handled=false;
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
                              char ops[][128], size_t n,
                              RvParsedIsaPolicy *policy) {
    const char *op = stmt->op;
    MiniAsMCInst *m;
    MiniAsSymbolExpr expr;
    uint64_t bits = 0U;
    bool fits = false;
    int64_t signed_value = 0;
    int rd = -1;
    unsigned family, slots;
    MiniAsRiscvSymbolModifier modifier = MINIAS_RV_SYM_NONE;
    policy->handled = true;
    if (strcmp(op, "li") == 0) {
        if (n != 2U || (rd = reg_number(ops[0])) < 0 ||
            !parse_li_bits(ops[1], &bits, &fits, &signed_value))
            goto invalid;
        family = RV_MC_LI;
        slots = 3U;
    } else if (strcmp(op, "call") == 0 || strcmp(op, "tail") == 0) {
        if (n != 1U || !parse_call_target(ops[0], &expr, &modifier))
            goto invalid;
        family = strcmp(op, "call") == 0 ? RV_MC_CALL : RV_MC_TAIL;
        slots = 1U;
    } else if (strcmp(op, "lla") == 0 || strcmp(op, "la") == 0) {
        if (n != 2U || (rd = reg_number(ops[0])) < 0 ||
            !minias_parse_symbol_addend(ops[1], &expr)) goto invalid;
        family = RV_MC_LA;
        slots = 2U;
    } else {
        policy->handled = false;
        return true;
    }
    m = minias_mcinst_alloc(slots);
    if (m == NULL) {
        minias_set_error(as, "out-of-memory:riscv-multi-mcinst");
        return false;
    }
    m->opcode = family;
    m->count = slots;
    if (family == RV_MC_CALL || family == RV_MC_TAIL)
        m->target_form = (uint32_t)modifier;
    if (family == RV_MC_LI) {
        m->target_form = fits ? RV_PSEUDO_LI_SIGNED32 : RV_PSEUDO_LI_WIDE;
        rv_set_reg(&m->operands[0], rd);
        rv_set_imm(&m->operands[1], (int64_t)bits);
        rv_set_imm(&m->operands[2], signed_value);
    } else {
        unsigned symbol_index = family == RV_MC_LA ? 1U : 0U;
        if (family == RV_MC_LA) {
            rv_set_reg(&m->operands[0], rd);
            m->target_form = strcmp(op, "la") == 0 ?
                             RV_PSEUDO_LA : RV_PSEUDO_LLA;
        }
        if (!rv_bind_symbol_operand(as, m, symbol_index, &expr)) {
            minias_mcinst_destroy(m);
            return false;
        }
    }
    stmt->mcinst = m;
    if (family == RV_MC_LI) {
        MiniAsRiscvLiPlan *plan = malloc(sizeof(*plan));
        if (plan == NULL) {
            minias_set_error(as, "out-of-memory:riscv-li-lowering-plan");
            stmt->mcinst = NULL;
            minias_mcinst_destroy(m);
            return false;
        }
        /* The generic MCInst destructor owns the immutable Target payload.
         * Re-layout never destroys/reparses the plan; source replay creates
         * a fresh assembly with separately owned MCInst objects. */
        m->target_payload = plan;
        m->dispose_target_payload = free;
        if (!rv_materialize_li(as, stmt, plan->words, &plan->count)) {
            stmt->mcinst = NULL;
            minias_mcinst_destroy(m);
            return false;
        }
    }
    return true;
invalid:
    minias_set_error(as, "bad-riscv-pseudo-mcinst:%s:%s:line=%zu",
                     op, stmt->args, stmt->line);
    return false;
}

static bool rv_parse_vector_mc(MiniAs *as, MiniAsStmt *stmt,
                               char operands[][128], size_t count,
                               RvParsedIsaPolicy *policy) {
    bool store;
    uint32_t width;
    int vd, rs1, rs2;
    char base[128];
    int64_t off;
    policy->handled = true;
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
    policy->handled = false;
    return true;
invalid:
    minias_set_error(as, "bad-vector-mcinst:%s:%s:line=%zu",
                     stmt->op, stmt->args, stmt->line);
    return false;
}

static bool rv_parse_atomic_mc(MiniAs *as, MiniAsStmt *stmt,
                               char operands[][128], size_t count,
                               RvParsedIsaPolicy *policy) {
    bool lr = false, recognized;
    uint32_t width, ordering, funct5;
    int rd, rs1, rs2;
    char base[128];
    int64_t off;
    recognized = decode_lr_sc_mnemonic(stmt->op, &lr, &width, &ordering);
    if (recognized) funct5 = lr ? 2U : 3U;
    else recognized = decode_amo_mnemonic(stmt->op,
                                          &funct5, &width, &ordering);
    policy->handled = recognized;
    if (!recognized) return true;
    /* One width decoder drives both opcode bits and RV32/RV64 policy. */
    policy->rv64_only = width == 3U;
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

enum { RV_CSR_READ, RV_CSR_FULL, RV_CSR_ALIAS };
typedef struct RvCsrDesc {
    const char *name;
    uint8_t funct3;
    uint8_t shape;
    bool immediate;
} RvCsrDesc;
static const RvCsrDesc rv_csr_forms[] = {
    {"csrr",   2U, RV_CSR_READ,  false},
    {"csrrw",  1U, RV_CSR_FULL,  false},
    {"csrrs",  2U, RV_CSR_FULL,  false},
    {"csrrc",  3U, RV_CSR_FULL,  false},
    {"csrrwi", 5U, RV_CSR_FULL,  true},
    {"csrrsi", 6U, RV_CSR_FULL,  true},
    {"csrrci", 7U, RV_CSR_FULL,  true},
    {"csrw",   1U, RV_CSR_ALIAS, false},
    {"csrs",   2U, RV_CSR_ALIAS, false},
    {"csrc",   3U, RV_CSR_ALIAS, false},
    {"csrwi",  5U, RV_CSR_ALIAS, true},
    {"csrsi",  6U, RV_CSR_ALIAS, true},
    {"csrci",  7U, RV_CSR_ALIAS, true}
};

static const RvCsrDesc *rv_find_csr_form(const char *name) {
    size_t i;
    for (i = 0U; i < sizeof(rv_csr_forms) / sizeof(rv_csr_forms[0]); ++i)
        if (strcmp(rv_csr_forms[i].name, name) == 0)
            return &rv_csr_forms[i];
    return NULL;
}

static bool rv_parse_csr_mc(MiniAs *as, MiniAsStmt *stmt,
                            char operands[][128], size_t count,
                            RvParsedIsaPolicy *policy) {
    const RvCsrDesc *form = rv_find_csr_form(stmt->op);
    const char *source = NULL;
    uint32_t csr, funct3;
    int rd = 0, rs1 = 0;
    int64_t zimm;
    bool immediate = false;

    policy->handled = form != NULL;
    if (form == NULL) return true;
    funct3 = form->funct3;
    if (form->shape == RV_CSR_READ) {
        if (count != 2U || (rd = reg_number(operands[0])) < 0 ||
            !parse_csr(operands[1], &csr)) goto invalid;
    } else if (form->shape == RV_CSR_FULL) {
        if (count != 3U || (rd = reg_number(operands[0])) < 0 ||
            !parse_csr(operands[1], &csr)) goto invalid;
        source = operands[2];
    } else {
        if (count != 2U || !parse_csr(operands[0], &csr)) goto invalid;
        source = operands[1];
    }
    if (source != NULL) {
        bool numeric = parse_i64(source, &zimm);
        if (form->immediate || numeric) {
            if (!numeric || zimm < 0 || zimm > 31)
                goto invalid;
            immediate = true;
            if (!form->immediate) funct3 |= 4U;
            rs1 = (int)zimm;
        } else if ((rs1 = reg_number(source)) < 0) goto invalid;
    }
    if (!rv_mc_word(as, stmt, 0x73U | (funct3 << 12U) |
                    (csr << 20U), rd, rs1, 0)) return false;
    /* zimm occupies the same five instruction bits as rs1, but it is NOT
     * an integer register and must not trip RV32E's x16..x31 restriction. */
    if (immediate) rv_set_imm(&stmt->mcinst->operands[1], rs1);
    return true;
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

/* One target-owned FP form table is the authority for recognition, encoding,
 * register classes, D-vs-F ISA feature, XLEN and optional rounding syntax.
 * Previously each of these dimensions enumerated overlapping mnemonic
 * lists; a new instruction could parse successfully but carry the wrong ISA
 * policy.  FPR mask: bit 0=rd, bit 1=rs1, bit 2=rs2. */
typedef enum RvFpKind {
    RV_FP_BINARY, RV_FP_FUSED, RV_FP_R3_FIXED, RV_FP_ALIAS,
    RV_FP_COMPARE, RV_FP_MOVE,
    RV_FP_CONVERT, RV_FP_LOAD, RV_FP_STORE
} RvFpKind;

typedef struct RvFpDesc {
    const char *name;
    RvFpKind kind;
    uint32_t encoding;
    uint64_t required_features;
    bool rv64_only;
    unsigned fpr_mask;
    unsigned default_rm;
    bool swap_sources;
} RvFpDesc;

#define RVFP(name, kind, encoding, feature, only64, mask, rm) \
    {name, RV_FP_##kind, encoding, RISCV_ISA_##feature, only64, mask, rm, false}
#define RVFP_SWAPPED(name, encoding, feature) \
    {name, RV_FP_COMPARE, encoding, RISCV_ISA_##feature, false, 6U, 0U, true}
static const RvFpDesc rv_fp_forms[] = {
    RVFP("fmadd.s",FUSED,0x00000043U,F,false,15U,7U),
    RVFP("fmsub.s",FUSED,0x00000047U,F,false,15U,7U),
    RVFP("fnmsub.s",FUSED,0x0000004bU,F,false,15U,7U),
    RVFP("fnmadd.s",FUSED,0x0000004fU,F,false,15U,7U),
    RVFP("fmadd.d",FUSED,0x02000043U,D,false,15U,7U),
    RVFP("fmsub.d",FUSED,0x02000047U,D,false,15U,7U),
    RVFP("fnmsub.d",FUSED,0x0200004bU,D,false,15U,7U),
    RVFP("fnmadd.d",FUSED,0x0200004fU,D,false,15U,7U),
    RVFP("fadd.s",BINARY,0x00000053U,F,false,7U,7U),
    RVFP("fsub.s",BINARY,0x08000053U,F,false,7U,7U),
    RVFP("fmul.s",BINARY,0x10000053U,F,false,7U,7U),
    RVFP("fdiv.s",BINARY,0x18000053U,F,false,7U,7U),
    RVFP("fsqrt.s",CONVERT,0x58000053U,F,false,3U,7U),
    RVFP("fadd.d",BINARY,0x02000053U,D,false,7U,7U),
    RVFP("fsub.d",BINARY,0x0a000053U,D,false,7U,7U),
    RVFP("fmul.d",BINARY,0x12000053U,D,false,7U,7U),
    RVFP("fdiv.d",BINARY,0x1a000053U,D,false,7U,7U),
    RVFP("fsqrt.d",CONVERT,0x5a000053U,D,false,3U,7U),
    RVFP("fsgnj.s",R3_FIXED,0x20000053U,F,false,7U,0U),
    RVFP("fsgnjn.s",R3_FIXED,0x20001053U,F,false,7U,0U),
    RVFP("fsgnjx.s",R3_FIXED,0x20002053U,F,false,7U,0U),
    RVFP("fmin.s",R3_FIXED,0x28000053U,F,false,7U,0U),
    RVFP("fmax.s",R3_FIXED,0x28001053U,F,false,7U,0U),
    RVFP("fsgnj.d",R3_FIXED,0x22000053U,D,false,7U,0U),
    RVFP("fsgnjn.d",R3_FIXED,0x22001053U,D,false,7U,0U),
    RVFP("fsgnjx.d",R3_FIXED,0x22002053U,D,false,7U,0U),
    RVFP("fmin.d",R3_FIXED,0x2a000053U,D,false,7U,0U),
    RVFP("fmax.d",R3_FIXED,0x2a001053U,D,false,7U,0U),
    /* GAS aliases retain the same ISA feature and FP register classes.
     * The sign-injection operation intentionally reads the source twice. */
    RVFP("fmv.s",ALIAS,0x20000053U,F,false,7U,0U),
    RVFP("fneg.s",ALIAS,0x20001053U,F,false,7U,0U),
    RVFP("fabs.s",ALIAS,0x20002053U,F,false,7U,0U),
    RVFP("fmv.d",ALIAS,0x22000053U,D,false,7U,0U),
    RVFP("fneg.d",ALIAS,0x22001053U,D,false,7U,0U),
    RVFP("fabs.d",ALIAS,0x22002053U,D,false,7U,0U),
    RVFP("feq.s",COMPARE,0xa0002053U,F,false,6U,0U),
    RVFP("flt.s",COMPARE,0xa0001053U,F,false,6U,0U),
    RVFP("fle.s",COMPARE,0xa0000053U,F,false,6U,0U),
    RVFP("feq.d",COMPARE,0xa2002053U,D,false,6U,0U),
    RVFP("flt.d",COMPARE,0xa2001053U,D,false,6U,0U),
    RVFP("fle.d",COMPARE,0xa2000053U,D,false,6U,0U),
    RVFP_SWAPPED("fgt.s",0xa0001053U,F),
    RVFP_SWAPPED("fge.s",0xa0000053U,F),
    RVFP_SWAPPED("fgt.d",0xa2001053U,D),
    RVFP_SWAPPED("fge.d",0xa2000053U,D),
    RVFP("fclass.s",MOVE,0xe0001053U,F,false,2U,0U),
    RVFP("fclass.d",MOVE,0xe2001053U,D,false,2U,0U),
    RVFP("fmv.x.d",MOVE,0xe2000053U,D,true,2U,0U),
    RVFP("fmv.x.w",MOVE,0xe0000053U,F,false,2U,0U),
    RVFP("fmv.d.x",MOVE,0xf2000053U,D,true,1U,0U),
    RVFP("fmv.w.x",MOVE,0xf0000053U,F,false,1U,0U),
    RVFP("fcvt.d.w",CONVERT,0xd2000053U,D,false,1U,0U),
    RVFP("fcvt.d.wu",CONVERT,0xd2100053U,D,false,1U,0U),
    RVFP("fcvt.d.l",CONVERT,0xd2200053U,D,true,1U,7U),
    RVFP("fcvt.d.lu",CONVERT,0xd2300053U,D,true,1U,7U),
    RVFP("fcvt.w.d",CONVERT,0xc2000053U,D,false,2U,7U),
    RVFP("fcvt.wu.d",CONVERT,0xc2100053U,D,false,2U,7U),
    RVFP("fcvt.l.d",CONVERT,0xc2200053U,D,true,2U,7U),
    RVFP("fcvt.lu.d",CONVERT,0xc2300053U,D,true,2U,7U),
    RVFP("fcvt.d.s",CONVERT,0x42000053U,D,false,3U,0U),
    RVFP("fcvt.s.d",CONVERT,0x40100053U,D,false,3U,7U),
    RVFP("fcvt.s.w",CONVERT,0xd0000053U,F,false,1U,7U),
    RVFP("fcvt.s.wu",CONVERT,0xd0100053U,F,false,1U,7U),
    RVFP("fcvt.s.l",CONVERT,0xd0200053U,F,true,1U,7U),
    RVFP("fcvt.s.lu",CONVERT,0xd0300053U,F,true,1U,7U),
    RVFP("fcvt.w.s",CONVERT,0xc0000053U,F,false,2U,7U),
    RVFP("fcvt.wu.s",CONVERT,0xc0100053U,F,false,2U,7U),
    RVFP("fcvt.l.s",CONVERT,0xc0200053U,F,true,2U,7U),
    RVFP("fcvt.lu.s",CONVERT,0xc0300053U,F,true,2U,7U),
    RVFP("flw",LOAD,0x00002007U,F,false,1U,0U),
    RVFP("fld",LOAD,0x00003007U,D,false,1U,0U),
    RVFP("fsw",STORE,0x00002027U,F,false,1U,0U),
    RVFP("fsd",STORE,0x00003027U,D,false,1U,0U)
};
#undef RVFP
#undef RVFP_SWAPPED

static const RvFpDesc *rv_find_fp_form(const char *op) {
    size_t i;
    for (i = 0U; i < sizeof(rv_fp_forms) / sizeof(rv_fp_forms[0]); ++i)
        if (strcmp(rv_fp_forms[i].name, op) == 0)
            return &rv_fp_forms[i];
    return NULL;
}

static bool rv_parse_fp_mc(MiniAs *as, MiniAsStmt *stmt,
                            char operands[][128], size_t count,
                            RvParsedIsaPolicy *policy) {
    const RvFpDesc *form = rv_find_fp_form(stmt->op);
    uint32_t base, rm;
    int rd = -1, rs1 = -1, rs2 = 0;
    char memory_base[128];
    int64_t offset;
    MiniAsSymbolExpr absolute_expr;
    bool absolute_lo = false;
    if (form == NULL) {
        policy->handled = false;
        return true;
    }
    policy->handled = true;
    policy->required_features = form->required_features;
    policy->rv64_only = form->rv64_only;
    base = form->encoding;
    rm = form->default_rm;
    if (form->kind == RV_FP_FUSED) {
        MiniAsMCInst *m;
        int rs3;
        unsigned i;
        if ((count != 4U && count != 5U) ||
            (rd = float_reg_number(operands[0])) < 0 ||
            (rs1 = float_reg_number(operands[1])) < 0 ||
            (rs2 = float_reg_number(operands[2])) < 0 ||
            (rs3 = float_reg_number(operands[3])) < 0 ||
            (count == 5U && !parse_fp_rounding_mode(operands[4], &rm)))
            goto invalid;
        m = minias_mcinst_alloc(4U);
        if (m == NULL) {
            minias_set_error(as, "out-of-memory:riscv-fp-fused");
            return false;
        }
        m->opcode = RV_MC_WORD;
        m->encoding = base | (rm << 12U);
        m->count = 4U;
        rv_set_reg(&m->operands[0], rd);
        rv_set_reg(&m->operands[1], rs1);
        rv_set_reg(&m->operands[2], rs2);
        rv_set_reg(&m->operands[3], rs3);
        for (i = 0U; i < 4U; ++i)
            m->operands[i].reg_class = MINIAS_MC_REG_FPR;
        stmt->mcinst = m;
        return true;
    }
    if (form->kind == RV_FP_LOAD || form->kind == RV_FP_STORE) {
        MiniAsMCInst *m;
        if (count != 2U || (rd = float_reg_number(operands[0])) < 0)
            goto invalid;
        if (!parse_mem(operands[1], &offset, memory_base)) {
            if (!parse_absolute_lo_memory(operands[1], &absolute_expr,
                                          memory_base)) goto invalid;
            absolute_lo = true;
            offset = 0;
        }
        rs1 = reg_number(memory_base);
        if (rs1 < 0 || offset < -2048 || offset > 2047) goto invalid;
        m=minias_mcinst_alloc(3U);
        if (m==NULL) {
            minias_set_error(as,"out-of-memory:riscv-fp-memory");
            return false;
        }
        m->opcode=form->kind == RV_FP_STORE ? RV_MC_STORE : RV_MC_LOAD;
        m->encoding=base;
        m->count=3U;
        rv_set_reg(&m->operands[0],rd);
        m->operands[0].reg_class=MINIAS_MC_REG_FPR;
        rv_set_reg(&m->operands[1],rs1);
        if (absolute_lo) {
            m->target_form = form->kind == RV_FP_STORE ?
                             RV_ABS_RELOC_LO12_S : RV_ABS_RELOC_LO12_I;
            if (!rv_bind_symbol_operand(as, m, 2U, &absolute_expr)) {
                minias_mcinst_destroy(m);
                return false;
            }
        } else rv_set_imm(&m->operands[2],offset);
        stmt->mcinst=m;
        return true;
    }
    if (form->kind == RV_FP_BINARY || form->kind == RV_FP_R3_FIXED ||
        form->kind == RV_FP_COMPARE) {
        if (count != 3U &&
            !(form->kind == RV_FP_BINARY && count == 4U)) goto invalid;
        rd = (form->fpr_mask & 1U) != 0U ?
             float_reg_number(operands[0]) : reg_number(operands[0]);
        rs1 = float_reg_number(operands[1]);
        rs2 = float_reg_number(operands[2]);
        if (count == 4U && !parse_fp_rounding_mode(operands[3], &rm))
            goto invalid;
        if (form->kind == RV_FP_BINARY) base |= rm << 12U;
    } else if (form->kind == RV_FP_ALIAS) {
        if (count != 2U ||
            (rd = float_reg_number(operands[0])) < 0 ||
            (rs1 = float_reg_number(operands[1])) < 0) goto invalid;
        rs2 = rs1;
    } else if (form->kind == RV_FP_MOVE || form->kind == RV_FP_CONVERT) {
        if (count != 2U &&
            !(form->kind == RV_FP_CONVERT && count == 3U)) goto invalid;
        rd = (form->fpr_mask & 1U) != 0U ?
             float_reg_number(operands[0]) : reg_number(operands[0]);
        rs1 = (form->fpr_mask & 2U) != 0U ?
              float_reg_number(operands[1]) : reg_number(operands[1]);
        if (count == 3U && !parse_fp_rounding_mode(operands[2], &rm))
            goto invalid;
        if (form->kind == RV_FP_CONVERT) base |= rm << 12U;
    } else {
        minias_set_error(as, "internal:unhandled-riscv-fp-descriptor");
        return false;
    }

    if (rd < 0 || rs1 < 0 || rs2 < 0) goto invalid;
    if (form->swap_sources) {
        const int reversed=rs1;
        rs1=rs2;
        rs2=reversed;
    }
    return rv_mc_fp_word(as,stmt,base,rd,rs1,rs2,form->fpr_mask);
invalid:
    minias_set_error(as,"bad-fp-mcinst:%s:%s:line=%zu",
                     stmt->op,stmt->args,stmt->line);
    return false;
}

/* Target-private opcode/form registry for pseudo, privileged and extension
 * forms not represented by the fixed RvMCDesc encoding table. Exactly one
 * parser family is selected per mnemonic: no repeated speculative parsing,
 * no successful parse that defers unknown-opcode diagnostics to layout.
 * Exact aliases take precedence over broad extension-family prefixes. */
#define RV_FORM(name, parser) {name, parser, false, 0U, false, RV_SIMPLE_NONE, 0U, 0U, false}
#define RV_EXTENSION(name, parser, feature) {name, parser, false, feature, false, RV_SIMPLE_NONE, 0U, 0U, false}
#define RV_EXT64(name, parser, feature) {name, parser, false, feature, true, RV_SIMPLE_NONE, 0U, 0U, false}
#define RV_PRIVILEGED(name, parser) {name, parser, false, UINT64_MAX, false, RV_SIMPLE_NONE, 0U, 0U, false}
#define RV_PREFIX(name, parser, feature) {name, parser, true, feature, false, RV_SIMPLE_NONE, 0U, 0U, false}
#define RV_ZERO(name, word, cword, feature) {name, rv_parse_simple_mc, false, feature, false, RV_SIMPLE_ZERO, word, cword, false}
#define RV_ZERO_PRIV(name, word) RV_ZERO(name, word, 0U, UINT64_MAX)
#define RV_TWO_REG(name, word, rs2) {name, rv_parse_simple_mc, false, 0U, false, RV_SIMPLE_TWO_REG, word, 0U, rs2}
#define RV_TWO_REG64(name, word, rs2) {name, rv_parse_simple_mc, false, 0U, true, RV_SIMPLE_TWO_REG, word, 0U, rs2}
#define RV_JR(name, word) {name, rv_parse_simple_mc, false, 0U, false, RV_SIMPLE_JR, word, 0U, false}
#define RV_FCSR(name, csr, kind) {name, rv_parse_simple_mc, false, RISCV_ISA_F | RISCV_ISA_ZICSR, false, RV_SIMPLE_FCSR_##kind, csr, 0U, false}
static const RvParseForm rv_parse_forms[] = {
    RV_FORM(".insn", rv_parse_simple_mc),
    RV_EXTENSION("c.li", rv_parse_simple_mc, RISCV_ISA_C),
    RV_ZERO("nop", 0x00000013U, 0x0001U, 0U),
    RV_ZERO("ret", 0x00008067U, 0x8082U, 0U),
    RV_ZERO_PRIV("sret", 0x10200073U),
    RV_ZERO("ecall", 0x00000073U, 0U, 0U),
    RV_ZERO("ebreak", 0x00100073U, 0U, 0U),
    RV_ZERO_PRIV("wfi", 0x10500073U),
    RV_ZERO_PRIV("pause", 0x0100000fU),
    RV_ZERO("fence.i", 0x0000100fU, 0U, RISCV_ISA_ZIFENCEI),
    RV_FORM("fence", rv_parse_simple_mc),
    RV_PRIVILEGED("sfence.vma", rv_parse_simple_mc),
    RV_FORM("jalr", rv_parse_simple_mc),
    RV_TWO_REG("mv", 0x00000013U, false),
    RV_TWO_REG("move", 0x00000013U, false),
    RV_TWO_REG("snez", 0x00003033U, true),
    RV_TWO_REG("seqz", 0x00103013U, false),
    RV_TWO_REG("neg", 0x40000033U, true),
    RV_TWO_REG64("negw", 0x4000003bU, true),
    RV_TWO_REG("not", 0xfff04013U, false),
    RV_JR("jr", 0x00000067U),
    RV_FCSR("fscsr", 3U, REG), RV_FCSR("frcsr", 3U, READ),
    RV_FCSR("frrm", 2U, READ), RV_FCSR("frflags", 1U, READ),
    RV_FCSR("fsrm", 2U, REG), RV_FCSR("fsflags", 1U, REG),
    RV_FCSR("fsrmi", 2U, IMMEDIATE),
    RV_FCSR("fsflagsi", 1U, IMMEDIATE),
    RV_FORM("li", rv_parse_multi_mc),
    RV_FORM("call", rv_parse_multi_mc),
    RV_FORM("tail", rv_parse_multi_mc),
    RV_FORM("lla", rv_parse_multi_mc),
    RV_FORM("la", rv_parse_multi_mc),
    RV_EXTENSION("vmv.v.i", rv_parse_vector_mc, RISCV_ISA_V),
    RV_EXTENSION("vsetvli", rv_parse_vector_mc, RISCV_ISA_V),
    RV_EXTENSION("vsetvl", rv_parse_vector_mc, RISCV_ISA_V),
    /* These families have widths/ordering or FP rounding modifiers. Their
     * existing decoders validate the COMPLETE suffix, not just the stem. */
    RV_PREFIX("vle", rv_parse_vector_mc, RISCV_ISA_V),
    RV_PREFIX("vse", rv_parse_vector_mc, RISCV_ISA_V),
    RV_PREFIX("amo", rv_parse_atomic_mc, RISCV_ISA_A),
    RV_PREFIX("lr.", rv_parse_atomic_mc, RISCV_ISA_A),
    RV_PREFIX("sc.", rv_parse_atomic_mc, RISCV_ISA_A),
    RV_PREFIX("f", rv_parse_fp_mc, RISCV_ISA_F)
};
#undef RV_FORM
#undef RV_EXTENSION
#undef RV_EXT64
#undef RV_PRIVILEGED
#undef RV_PREFIX
#undef RV_ZERO
#undef RV_ZERO_PRIV
#undef RV_TWO_REG
#undef RV_TWO_REG64
#undef RV_JR
#undef RV_FCSR

static const RvParseForm *rv_find_parse_form(const char *op) {
    static const RvParseForm csr_family = {
        "<csr>", rv_parse_csr_mc, false, RISCV_ISA_ZICSR, false,
        RV_SIMPLE_NONE, 0U, 0U, false
    };
    size_t i;
    for (i = 0U; i < sizeof(rv_parse_forms)/sizeof(rv_parse_forms[0]); ++i)
        if (!rv_parse_forms[i].prefix &&
            strcmp(op, rv_parse_forms[i].spelling) == 0)
            return &rv_parse_forms[i];
    if (rv_find_csr_form(op) != NULL) return &csr_family;
    for (i = 0U; i < sizeof(rv_parse_forms)/sizeof(rv_parse_forms[0]); ++i)
        if (rv_parse_forms[i].prefix &&
            strncmp(op, rv_parse_forms[i].spelling,
                    strlen(rv_parse_forms[i].spelling)) == 0)
            return &rv_parse_forms[i];
    return NULL;
}

static bool rv_parse_mcinst_impl(MiniAs *as, MiniAsStmt *stmt,
                                  RvParsedIsaPolicy *policy) {
    const RvMCDesc *desc;
    char operands[8][128];
    size_t count;
    int rd = -1, rs1 = -1, rs2 = -1;
    int64_t imm = 0;
    bool is_imm = false;
    MiniAsMCInst *inst;
    unsigned family, slots;
    unsigned form_flags;
    uint32_t encoding;
    char base[128];
    const char *op = stmt->op;
    MiniAsSymbolExpr absolute_expr;
    unsigned absolute_reloc = RV_ABS_RELOC_NONE;

    desc = rv_find_mc_descriptor(op);
    if (desc == NULL) {
        const RvParseForm *form = rv_find_parse_form(op);
        bool accepted;
        if (form == NULL) {
            minias_set_error(as, "unsupported-instruction:%s:line=%zu",
                             op, stmt->line);
            return false;
        }
        if(!split_operands(stmt->args,operands,8U,&count))goto invalid;
        policy->handled = false;
        policy->required_features = form->required_features;
        policy->rv64_only = form->rv64_only;
        policy->selected_form = form;
        accepted = form->parse(as, stmt, operands, count, policy);
        if (!accepted) return false;
        if (!policy->handled) {
            minias_set_error(as, "unsupported-instruction:%s:line=%zu",
                             op, stmt->line);
            return false;
        }
        return as->error[0] == '\0';
    }
    family = desc->family;
    encoding = desc->base;
    form_flags = desc->form_flags;
    policy->required_features = desc->required_features;
    policy->rv64_only = desc->rv64_only;
    if(!split_operands(stmt->args,operands,8U,&count))goto invalid;

    if (family == RV_MC_R || family == RV_MC_I) {
        bool two = (form_flags & RV_FORM_TWO_OPERANDS)!=0U;
        if (count != (two ? 2U : 3U)) goto invalid;
        if (two) {
            rd = reg_number(operands[0]);
            rs1 = reg_number(operands[1]);
            /* Ratified Zbb gives rev8 distinct OP-IMM words for RV32 and
             * RV64.  Its XLEN is selected by the immutable Target profile,
             * not inferred from the mnemonic or the host word size. */
            if ((form_flags & RV_FORM_REV8_XLEN)!=0U &&
                MINIAS_RV_STATE(as)->target_xlen == 32U)
                encoding = UINT32_C(0x69805013);
            imm = (int64_t)((encoding >> 20U) & 0xfffU);
            family = RV_MC_I;
        } else if (count == 3U) {
            rd = reg_number(operands[0]);
            rs1 = reg_number(operands[1]);
            if (family == RV_MC_R) {
                rs2 = reg_number(operands[2]);
                if (rs2 < 0 &&
                    (form_flags & RV_FORM_ALT_IMMEDIATE)!=0U &&
                    parse_i64(operands[2], &imm)) {
                    family = RV_MC_I;
                    is_imm = true;
                    encoding = 0x13U | (encoding & 0x7000U);
                    if ((form_flags & RV_FORM_NEGATE_IMMEDIATE)!=0U) {
                        if (imm == INT64_MIN) goto invalid;
                        imm = -imm;
                    } else if ((form_flags & RV_FORM_ARITH_IMMEDIATE)!=0U) {
                        encoding |= 0x40000000U;
                    }
                }
            } else {
                is_imm = parse_i64(operands[2], &imm);
                if (!is_imm &&
                    parse_absolute_modifier(operands[2], "%lo(",
                                            &absolute_expr)) {
                    absolute_reloc = RV_ABS_RELOC_LO12_I;
                    is_imm = true;
                    imm = 0;
                }
            }
        }
        if (rd < 0 || rs1 < 0 || (family == RV_MC_R && rs2 < 0) ||
            (family == RV_MC_I && !is_imm && count != 2U &&
             !parse_i64(operands[2], &imm))) goto invalid;
        if (family == RV_MC_I) {
            bool shift=(form_flags & RV_FORM_SHIFT_IMMEDIATE)!=0U;
            if (shift) {
                int max = ((encoding & 0x7fU) == 0x1bU ||
                           MINIAS_RV_STATE(as)->target_xlen == 32U) ? 31 : 63;
                if (imm < 0 || imm > max) goto invalid;
            } else if ((form_flags & RV_FORM_FIXED_IMMEDIATE)==0U &&
                       (imm < -2048 || imm > 2047)) goto invalid;
        }
        if ((form_flags & RV_FORM_SWAP_SOURCES)!=0U) {
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
        } else if (count == 2U &&
                   parse_absolute_lo_memory(operands[1], &absolute_expr,
                                            base)) {
            rs1 = reg_number(base);
            if (rs1 < 0) goto invalid;
            absolute_reloc = family == RV_MC_LOAD ?
                             RV_ABS_RELOC_LO12_I : RV_ABS_RELOC_LO12_S;
            imm = 0;
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
            inst = minias_mcinst_alloc(slots);
            if (inst == NULL) goto oom;
            inst->opcode = family == RV_MC_LOAD ?
                           RV_MC_SYMBOL_LOAD : RV_MC_SYMBOL_STORE;
            inst->encoding = encoding;
            inst->count = slots;
            rv_set_reg(&inst->operands[0], rd);
            rv_set_reg(&inst->operands[1], rs1);
            if (!rv_bind_symbol_operand(as, inst, 2U, &expr)) {
                minias_mcinst_destroy(inst);
                return false;
            }
            stmt->mcinst = inst;
            return true;
        }
    } else if (family == RV_MC_U) {
        if (count != 2U) goto invalid;
        rd = reg_number(operands[0]);
        if (rd < 0) goto invalid;
        if (!parse_i64(operands[1], &imm)) {
            if ((form_flags & RV_FORM_HI_RELOCATION)==0U ||
                !parse_absolute_modifier(operands[1], "%hi(",
                                         &absolute_expr)) goto invalid;
            absolute_reloc = RV_ABS_RELOC_HI20;
            imm = 0;
        }
        slots = 2U;
    } else if (family == RV_MC_BRANCH) {
        bool two = (form_flags & RV_FORM_BRANCH_ZERO)!=0U;
        const char *symbol_text;
        if (count != (two ? 2U : 3U)) goto invalid;
        rs1 = reg_number(operands[0]);
        if (rs1 < 0) goto invalid;
        if (two) {
            rs2 = 0;
            symbol_text = operands[1];
            if ((form_flags & RV_FORM_BRANCH_ZERO_RS1)!=0U) {
                rs2 = rs1; rs1 = 0;
            }
        } else {
            rs2 = reg_number(operands[1]);
            if (rs2 < 0) goto invalid;
            if ((form_flags & RV_FORM_BRANCH_REVERSE)!=0U) {
                int tmp=rs1;rs1=rs2;rs2=tmp;
            }
            symbol_text = operands[2];
        }
        {
            MiniAsSymbolExpr parsed;
            if (!minias_parse_symbol_addend(symbol_text,&parsed)) goto invalid;
            slots=3U;
            inst=minias_mcinst_alloc(slots);
            if (inst==NULL) goto oom;
            inst->opcode=family; inst->encoding=encoding; inst->count=slots;
            rv_set_reg(&inst->operands[0],rs1);
            rv_set_reg(&inst->operands[1],rs2);
            if (!rv_bind_symbol_operand(as, inst, 2U, &parsed)) {
                minias_mcinst_destroy(inst);
                return false;
            }
            stmt->mcinst=inst;
        }
        return true;
    } else if (family == RV_MC_JAL) {
        const char *sym;
        rd = (form_flags & RV_FORM_JAL_ZERO_RD)!=0U ? 0 : 1;
        if ((form_flags & RV_FORM_JAL_ZERO_RD)!=0U && count != 1U)
            goto invalid;
        if (count == 1U) sym = operands[0];
        else if (count == 2U && (form_flags & RV_FORM_JAL_ZERO_RD)==0U) {
            rd=reg_number(operands[0]);
            sym=operands[1];
        } else goto invalid;
        if (rd < 0) goto invalid;
        {
            MiniAsSymbolExpr parsed;
            if (!minias_parse_symbol_addend(sym,&parsed)) goto invalid;
            slots=2U;
            inst=minias_mcinst_alloc(slots);
            if(inst==NULL) goto oom;
            inst->opcode=family; inst->encoding=encoding; inst->count=slots;
            rv_set_reg(&inst->operands[0],rd);
            if (!rv_bind_symbol_operand(as, inst, 1U, &parsed)) {
                minias_mcinst_destroy(inst);
                return false;
            }
            stmt->mcinst=inst;
        }
        return true;
    } else {
        minias_set_error(as, "internal:riscv-unhandled-descriptor-family:%u",
                         family);
        return false;
    }

    inst=minias_mcinst_alloc(slots);
    if(inst==NULL)goto oom;
    inst->opcode=family;inst->encoding=encoding;inst->count=slots;
    rv_set_reg(&inst->operands[0],rd);
    if(family==RV_MC_R) {
        rv_set_reg(&inst->operands[1],rs1);
        rv_set_reg(&inst->operands[2],rs2);
    } else if(family==RV_MC_I) {
        rv_set_reg(&inst->operands[1],rs1);
        if (absolute_reloc == RV_ABS_RELOC_LO12_I) {
            inst->target_form = absolute_reloc;
            if (!rv_bind_symbol_operand(as, inst, 2U, &absolute_expr)) {
                minias_mcinst_destroy(inst);
                return false;
            }
        } else rv_set_imm(&inst->operands[2],imm);
    } else if(family==RV_MC_LOAD || family==RV_MC_STORE){
        rv_set_reg(&inst->operands[1],rs1);
        if (absolute_reloc != RV_ABS_RELOC_NONE) {
            inst->target_form = absolute_reloc;
            if (!rv_bind_symbol_operand(as, inst, 2U, &absolute_expr)) {
                minias_mcinst_destroy(inst);
                return false;
            }
        } else rv_set_imm(&inst->operands[2],imm);
    } else if (absolute_reloc == RV_ABS_RELOC_HI20) {
        inst->target_form = absolute_reloc;
        if (!rv_bind_symbol_operand(as, inst, 1U, &absolute_expr)) {
            minias_mcinst_destroy(inst);
            return false;
        }
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

/* The E ABI removes integer registers x16..x31, not symbols spelled a6,
 * s2, t3 or x16. All instruction forms, including aliases, CSR, vector,
 * FP and multiword pseudos, retain their actual register operands in MCInst.
 * Check those typed operands after parsing, never a lexical sweep over the
 * original assembly text. EF_RVE=8 also covers -mabi=ilp32e without -march. */
bool minias_riscv_parse_mcinst(MiniAs *as, MiniAsStmt *stmt) {
    const MiniAsRiscvState *state = MINIAS_RV_STATE(as);
    RvParsedIsaPolicy policy={0};
    const MiniAsMCInst *inst;
    size_t index;

    if (!rv_parse_mcinst_impl(as, stmt, &policy)) return false;
    inst = stmt->mcinst;
    if (inst == NULL) {
        minias_set_error(as, "internal:riscv-parse-form-without-mcinst");
        return false;
    }
    if (!minias_riscv_validate_parsed_isa(as, stmt,
                                          policy.required_features,
                                          policy.rv64_only)) return false;
    if (state == NULL ||
        ((state->isa_features & RISCV_ISA_E) == 0U &&
         (as->elf_flags & 8U) == 0U)) return true;
    for (index = 0U; index < inst->count; ++index) {
        const MiniAsMCOperand *operand = &inst->operands[index];
        if (operand->kind == MINIAS_MC_REGISTER &&
            operand->reg_class == MINIAS_MC_REG_GPR && operand->reg >= 16U) {
            minias_set_error(as, "riscv32e-invalid-register:%s:line=%zu",
                             stmt->op, stmt->line);
            return false;
        }
    }
    return true;
}


/* Typed variable-size RISC-V fragments: source operands were parsed once.
 * Layout owns size; this Target writes instructions and stages relocations
 * from MCInst, without reopening source or mutating final symbols/fixups. */
static bool rv_encode_multi_mc(MiniAs *as, const MiniAsStmt *stmt) {
    const MiniAsMCInst *m = stmt->mcinst;
    if (m->opcode == RV_MC_LI) {
        const MiniAsRiscvLiPlan *plan = m->target_payload;
        unsigned i;
        if (plan == NULL) {
            minias_set_error(as, "internal:riscv-li-plan-missing:line=%zu",
                             stmt->line);
            return false;
        }
        if (stmt->size != plan->count * 4U) {
            minias_set_error(as, "internal:riscv-li-plan-layout-mismatch:line=%zu",
                             stmt->line);
            return false;
        }
        for (i = 0U; i < plan->count; ++i)
            if (!append_u32(as, stmt->section, plan->words[i])) return false;
        return true;
    }

    if (m->opcode == RV_MC_CALL || m->opcode == RV_MC_TAIL) {
        const MiniAsSymbolRef *expr = m->operands[0].bound_symbol;
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
        /* Both CALL and TAIL retain @plt as a Target modifier. Existing
         * psABI lowering emits CALL_PLT even without an explicit suffix. */
        if (m->target_form != MINIAS_RV_SYM_NONE &&
            m->target_form != MINIAS_RV_SYM_PLT) {
            minias_set_error(as, "internal:riscv-call-modifier");
            return false;
        }
        return minias_encode_add_fixup_ref(as, stmt->section, stmt->offset,
                                MINIAS_FIXUP_RV_CALL_PLT, 4U, *expr, false);
    }

    if (m->opcode == RV_MC_LA) {
        const MiniAsSymbolRef *expr = m->operands[1].bound_symbol;
        const MiniAsSymbol *target;
        MiniAsSymbolRef anchor;
        int rd = (int)m->operands[0].reg;
        bool use_got;
        if (expr == NULL) {
            minias_set_error(as, "internal:riscv-la-symbol-missing");
            return false;
        }
        target = minias_symbol_ref_lookup(as, *expr);
        /* Linux early boot: NOPIC is default; PIC external la may use a
         * GOT reference only if the source did not request an addend. */
        if (m->target_form != RV_PSEUDO_LA &&
            m->target_form != RV_PSEUDO_LLA) {
            minias_set_error(as, "internal:riscv-la-invalid-form");
            return false;
        }
        use_got = (stmt->site.mode_bits & MINIAS_RISCV_OPTION_PIC) != 0U &&
                  m->target_form == RV_PSEUDO_LA && expr->addend == 0 &&
                  (target == NULL || !target->defined);
        if(!minias_encode_request_anchor_ref(as,stmt,"pcrel",
                                             &anchor)) {
            minias_set_error(as,"riscv-private-pcrel-symbol-overflow");
            return false;
        }

        if (!append_u32(as, stmt->section,
                        0x17U | ((uint32_t)rd << 7U))) return false;
        if (use_got) {
            if (!append_u32(as, stmt->section,
                            enc_i(0x03U, rd, 3U, rd, 0))) return false;
        } else if (!append_u32(as, stmt->section,
                               enc_i(0x13U, rd, 0U, rd, 0))) return false;
        return minias_encode_add_fixup_ref(as, stmt->section, stmt->offset,
                                use_got ? MINIAS_FIXUP_RV_GOT_HI20 :
                                          MINIAS_FIXUP_RV_PCREL_HI20,
                                4U, *expr, false) &&
               minias_encode_add_fixup_ref(as, stmt->section,
                                stmt->offset + 4U,
                                MINIAS_FIXUP_RV_PCREL_LO12_I, 4U, anchor,
                                false);
    }

    minias_set_error(as, "internal:unknown-riscv-multi-mcinst:%u",
                     m->opcode);
    return false;
}

static bool rv_encode_mcinst(MiniAs *as, const MiniAsStmt *stmt) {
    const MiniAsMCInst *m=stmt->mcinst;
    uint16_t compressed;
    if (m->opcode == RV_MC_RAW)
        return append_u32(as, stmt->section, m->encoding);
    if (rv_rvc_encode_plain(as,stmt,&compressed))
        return append_u16(as,stmt->section,compressed) &&
               minias_encode_add_elf_flags(as,1U);
    if (m->opcode == RV_MC_LI || m->opcode == RV_MC_CALL ||
        m->opcode == RV_MC_TAIL || m->opcode == RV_MC_LA)
        return rv_encode_multi_mc(as, stmt);
    if (m->opcode == RV_MC_C16) {
        /* The selected 16-bit instruction may have no register operands
         * (c.nop/c.jr), so dispatch before reading operand[0]. */
        if(!append_u16(as,stmt->section,(uint16_t)m->encoding))return false;
        return minias_encode_add_elf_flags(as,1U); /* EF_RISCV_RVC */
    }
    if (stmt->size==2U &&
        (m->opcode==RV_MC_BRANCH || m->opcode==RV_MC_JAL)) {
        MiniAsRiscvBranchPlan branch;
        const MiniAsSymbolRef *symbol=m->operands[
            m->opcode==RV_MC_BRANCH?2U:1U].bound_symbol;
        uint16_t bits;
        MiniAsFixupKind fixup;
        if(symbol==NULL || !minias_riscv_resolve_branch_plan(as,stmt,&branch))
            return false;
        if(m->opcode==RV_MC_BRANCH) {
            int64_t disp=branch.local?branch.displacement:0;
            if((disp&1)!=0 || disp< -256 || disp>254) {
                minias_set_error(as,"riscv-rvc-branch-range:line=%zu",stmt->line);
                return false;
            }
            bits=rv_rvc_branch_bits(disp,m->operands[0].reg,
                                     ((m->encoding>>12U)&7U)==1U);
            fixup=MINIAS_FIXUP_RV_RVC_BRANCH;
        } else {
            int64_t disp=branch.local?branch.displacement:0;
            if((disp&1)!=0 || disp< -2048 || disp>2046) {
                minias_set_error(as,"riscv-rvc-jump-range:line=%zu",stmt->line);
                return false;
            }
            bits=rv_rvc_jump_bits(disp);
            fixup=MINIAS_FIXUP_RV_RVC_JUMP;
        }
        return append_u16(as,stmt->section,bits) &&
               minias_encode_add_elf_flags(as,1U) &&
               minias_encode_add_fixup_ref(as,stmt->section,stmt->offset,
                                            fixup,2U,*symbol,false);
    }
    int rd=(int)m->operands[0].reg,rs1=0;
    uint32_t base=m->encoding,code=0U;
    const MiniAsSymbolRef *expr=NULL;
    if (m->count > 1U && m->operands[1].kind==MINIAS_MC_REGISTER)
        rs1=(int)m->operands[1].reg;
    if (m->opcode == RV_MC_SYMBOL_LOAD ||
        m->opcode == RV_MC_SYMBOL_STORE) {
        const bool load = m->opcode == RV_MC_SYMBOL_LOAD;
        const MiniAsSymbolRef *ref = m->operands[2].bound_symbol;
        MiniAsSymbolRef anchor;
        uint32_t second;
        if (ref == NULL) {
            minias_set_error(as, "internal:missing-riscv-symbol-fragment");
            return false;
        }
        if(!minias_encode_request_anchor_ref(as,stmt,
                                             load?"load":"store",&anchor)) {
            minias_set_error(as,"riscv-private-memory-symbol-overflow");
            return false;
        }
        second = base |
                 ((uint32_t)rs1 << 15U) |
                 ((uint32_t)rd << (load ? 7U : 20U));
        if (!append_u32(as, stmt->section,
                        0x17U | ((uint32_t)rs1 << 7U)) ||
            !append_u32(as, stmt->section, second))
            return false;
        return minias_encode_add_fixup_ref(as, stmt->section, stmt->offset,
                                MINIAS_FIXUP_RV_PCREL_HI20, 4U, *ref,
                                false) &&
               minias_encode_add_fixup_ref(as, stmt->section,
                                stmt->offset + 4U,
                                load ? MINIAS_FIXUP_RV_PCREL_LO12_I :
                                       MINIAS_FIXUP_RV_PCREL_LO12_S,
                                4U, anchor, false);
    }
    if(m->opcode==RV_MC_BRANCH || m->opcode==RV_MC_JAL) {
        MiniAsRiscvBranchPlan branch;
        expr=m->operands[m->opcode==RV_MC_BRANCH ? 2U : 1U].bound_symbol;
        if(!minias_riscv_resolve_branch_plan(as,stmt,&branch))return false;
        if(m->opcode==RV_MC_BRANCH && !branch.local){
            minias_set_error(as,"unsupported-relocation:%s:line=%zu",
                             stmt->op,stmt->line);
            return false;
        }
        if(branch.local){
            if(m->opcode==RV_MC_JAL){
                if(!branch.jal_legal){
                    minias_set_error(as,"jal-range:line=%zu",stmt->line);
                    return false;
                }
                return append_u32(as,stmt->section,
                                  enc_j(rd,branch.displacement)) &&
                       (!(MINIAS_RV_STATE(as)->isa_features&RISCV_ISA_C) ||
                        minias_encode_add_fixup_ref(as,stmt->section,
                            stmt->offset,MINIAS_FIXUP_RV_JAL,4U,*expr,false));
            }
            if(stmt->size==8U){
                if(!branch.long_legal){
                    minias_set_error(as,"branch-long-jal-range:%s:line=%zu",
                                     stmt->op,stmt->line);
                    return false;
                }
                return append_u32(as,stmt->section,
                           enc_b((base>>12U &7U)^1U,rd,rs1,8)) &&
                       append_u32(as,stmt->section,
                                  enc_j(0,branch.displacement-4));
            }
            if(!branch.short_legal){
                minias_set_error(as,"branch-range:%s:line=%zu",
                                 stmt->op,stmt->line);
                return false;
            }
            return append_u32(as,stmt->section,
                              enc_b((base>>12U)&7U,rd,rs1,
                                    branch.displacement)) &&
                   (!(MINIAS_RV_STATE(as)->isa_features&RISCV_ISA_C) ||
                    minias_encode_add_fixup_ref(as,stmt->section,stmt->offset,
                              MINIAS_FIXUP_RV_BRANCH,4U,*expr,false));
        }
        return append_u32(as,stmt->section,enc_j(rd,0)) &&
               minias_encode_add_fixup_ref(as,stmt->section,stmt->offset,
                                MINIAS_FIXUP_RV_JAL,4U,*expr,
                                false);
    } else if(!rv_scalar_machine_word(m,true,&code)) {
        minias_set_error(as,"unknown-riscv-mcinst");
        return false;
    }
    if (m->target_form != RV_ABS_RELOC_NONE) {
        MiniAsFixupKind kind;
        unsigned symbol_index;
        const MiniAsSymbolRef *symbol;
        uint32_t addend_low;
        switch (m->target_form) {
        case RV_ABS_RELOC_HI20:
            if (m->opcode != RV_MC_U) goto invalid_absolute_form;
            kind = MINIAS_FIXUP_RV_HI20;
            symbol_index = 1U;
            break;
        case RV_ABS_RELOC_LO12_I:
            if (m->opcode != RV_MC_I && m->opcode != RV_MC_LOAD)
                goto invalid_absolute_form;
            kind = MINIAS_FIXUP_RV_LO12_I;
            symbol_index = 2U;
            break;
        case RV_ABS_RELOC_LO12_S:
            if (m->opcode != RV_MC_STORE) goto invalid_absolute_form;
            kind = MINIAS_FIXUP_RV_LO12_S;
            symbol_index = 2U;
            break;
        default:
            goto invalid_absolute_form;
        }
        symbol = m->operands[symbol_index].bound_symbol;
        if (symbol == NULL) goto invalid_absolute_form;
        /* GAS retains the expression addend in the unresolved instruction
         * field as well as in ELF RELA.  The linker eventually replaces this
         * field using S+A, so these bits are a pre-relocation representation,
         * not an additional runtime addend.  Preserve the typed SymbolRef and
         * the ISA-specific U/I/S field layout rather than dropping A to zero. */
        addend_low = (uint32_t)symbol->addend & 0xfffU;
        if (kind == MINIAS_FIXUP_RV_HI20) {
            uint32_t hi = (uint32_t)(((uint64_t)symbol->addend + 0x800U)
                                     >> 12U) & 0xfffffU;
            code = (code & 0xfffU) | (hi << 12U);
        } else if (kind == MINIAS_FIXUP_RV_LO12_I) {
            code = (code & 0x000fffffU) | (addend_low << 20U);
        } else {
            code = (code & ~UINT32_C(0xfe000f80)) |
                   ((addend_low & 31U) << 7U) |
                   ((addend_low >> 5U) << 25U);
        }
        if (!append_u32(as, stmt->section, code)) return false;
        return minias_encode_add_fixup_ref(as,stmt->section,stmt->offset,
                                            kind,4U,*symbol,false);
    }
    return append_u32(as,stmt->section,code);
invalid_absolute_form:
    minias_set_error(as,"internal:bad-riscv-absolute-reloc-mcinst:%u",
                     m->target_form);
    return false;
}

bool minias_riscv_encode(MiniAs *as, const MiniAsStmt *stmt) {
    if (stmt->mcinst == NULL) {
        minias_set_error(as, "internal:riscv-missing-typed-instruction:%s:line=%zu",
                         stmt->op, stmt->line);
        return false;
    }
    return rv_encode_mcinst(as, stmt);
}
