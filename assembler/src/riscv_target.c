#include "minias_internal.h"
#include "riscv_target.h"
#include <stdint.h>
#include <string.h>

static bool is_conditional_branch_op(const char *op) {
    return strcmp(op, "beq") == 0 || strcmp(op, "bne") == 0 ||
           strcmp(op, "blt") == 0 || strcmp(op, "bge") == 0 ||
           strcmp(op, "bltu") == 0 || strcmp(op, "bgeu") == 0 ||
           strcmp(op, "beqz") == 0 || strcmp(op, "bnez") == 0 ||
           strcmp(op, "bltz") == 0 || strcmp(op, "bgez") == 0 ||
           strcmp(op, "bgtz") == 0 || strcmp(op, "blez") == 0 ||
           strcmp(op, "bgt") == 0 || strcmp(op, "ble") == 0 ||
           strcmp(op, "bgtu") == 0 || strcmp(op, "bleu") == 0;
}


/* GNU as fills RISC-V executable .align/.p2align/.balign gaps with
 * executable NOPs, never zero halfwords.  In arch/riscv/lib/strcmp.S the
 * .p2align 3 immediately precedes the aligned Zbb fast path: emitting
 * 0x0000 (C.UNIMP) here traps during early Linux boot even though all ELF
 * sections, exported symbols and relocations pass the basic oracle.
 *
 * Preserve the existing .org zero-fill semantics and data alignment. */
static bool minias_section_append_code_alignment(MiniAs *as,
                                                 int section_index,
                                                 size_t size) {
    static const unsigned char nop32[] = {0x13U, 0x00U, 0x00U, 0x00U};
    static const unsigned char nop16[] = {0x01U, 0x00U};

    if ((size & 1U) != 0U) {
        /* A raw byte in .text (e.g. embedded alternatives/data) can leave
         * an odd-sized alignment gap. No sequence of 16/32-bit RISC-V NOP
         * encodings fills an odd byte count; retain the previous zero-byte
         * fill for this non-instruction-shaped case, as GNU as does. */
        return minias_section_append_zero(as, section_index, size);
    }
    while (size >= sizeof(nop32)) {
        if (!minias_section_append(as, section_index, nop32, sizeof(nop32))) {
            return false;
        }
        size -= sizeof(nop32);
    }
    if (size == sizeof(nop16)) {
        return minias_section_append(as, section_index, nop16, sizeof(nop16));
    }
    return true;
}


static uint32_t riscv_reloc(unsigned width, MiniAsRelocKind kind) {
    if (kind == MINIAS_RELOC_ABS)
        return width == 4U ? MINIAS_R_RISCV_32 :
               width == 8U ? MINIAS_R_RISCV_64 : 0U;
    if (kind == MINIAS_RELOC_ADD)
        return width == 2U ? MINIAS_R_RISCV_ADD16 :
               width == 4U ? MINIAS_R_RISCV_ADD32 :
               width == 8U ? MINIAS_R_RISCV_ADD64 : 0U;
    if (kind == MINIAS_RELOC_SUB)
        return width == 2U ? MINIAS_R_RISCV_SUB16 :
               width == 4U ? MINIAS_R_RISCV_SUB32 :
               width == 8U ? MINIAS_R_RISCV_SUB64 : 0U;
    return 0U;
}

/* Validate raw 32-bit encodings AFTER macro/ISA pseudo expansion, section
 * padding and relocation placeholders. This protects all emission families,
 * including a newly added pseudo unknown to a handwritten opcode allowlist.
 * For strict-I executable sections, no raw .word/.insn/.org data is allowed:
 * every 4-byte word must be a real base-I instruction.
 *
 * This intentionally differs from the default legacy MiniAS input mode,
 * which supports RISC-V extension instructions and raw kernel assembly. */
static bool strict_base_i_word(uint32_t w, unsigned xlen) {
    uint32_t op = w & 127U;
    uint32_t f3 = (w >> 12U) & 7U;
    uint32_t f7 = (w >> 25U) & 127U;

    if ((w & 3U) != 3U) return false;
    switch (op) {
    case 0x17U: /* AUIPC */
    case 0x37U: /* LUI */
    case 0x6fU: /* JAL */
        return true;
    case 0x67U: /* JALR */
        return f3 == 0U;
    case 0x63U: /* branch */
        return f3 == 0U || f3 == 1U || f3 >= 4U;
    case 0x03U: /* RV32 LB/LH/LW/LBU/LHU; RV64 adds LD/LWU */
        return f3 <= 2U || f3 == 4U || f3 == 5U ||
               (xlen == 64U && (f3 == 3U || f3 == 6U));
    case 0x23U: /* RV32 SB/SH/SW; RV64 adds SD */
        return f3 <= (xlen == 32U ? 2U : 3U);
    case 0x13U: /* immediate, RV32 has only five shamt bits */
        if (f3 == 1U)
            return xlen == 32U ? (w >> 25U) == 0U : (w >> 26U) == 0U;
        if (f3 == 5U)
            return xlen == 32U ?
                ((w >> 25U) == 0U || (w >> 25U) == 32U) :
                ((w >> 26U) == 0U || (w >> 26U) == 16U);
        return f3 != 1U && f3 != 5U;
    case 0x1bU: /* RV64-only word-immediate */
        if (xlen != 64U) return false;
        if (f3 == 0U) return true;
        if (f3 == 1U) return f7 == 0U;
        if (f3 == 5U) return f7 == 0U || f7 == 32U;
        return false;
    case 0x33U: /* register ALU, excluding M */
        return f7 == 0U || (f7 == 32U && (f3 == 0U || f3 == 5U));
    case 0x3bU: /* RV64 word ALU, excluding M */
        if (xlen != 64U) return false;
        return (f7 == 0U && (f3 == 0U || f3 == 1U || f3 == 5U)) ||
               (f7 == 32U && (f3 == 0U || f3 == 5U));
    case 0x0fU: /* FENCE only, no FENCE.I/Ztso */
        /* PAUSE is encoded as a reserved FENCE hint (Zihintpause);
         * it is not an instruction of the explicit base-I profile. */
        return f3 == 0U && (w >> 28U) == 0U &&
               w != UINT32_C(0x0100000f);
    case 0x73U: /* ECALL, EBREAK only, no Zicsr or privileged ops */
        return w == UINT32_C(0x00000073) || w == UINT32_C(0x00100073);
    default:
        return false;
    }
}

static bool strict_base_i_check_sections(MiniAs *as) {
    size_t i;
    if (as->strict_i_xlen == 0U) return true;
    if (as->strict_i_xlen != 32U && as->strict_i_xlen != 64U) {
        minias_set_error(as, "unsupported-strict-riscv-xlen");
        return false;
    }
    for (i = 0U; i < as->section_count; ++i) {
        const MiniAsSection *section = &as->sections[i];
        size_t offset;
        if ((section->flags & MINIAS_SHF_EXECINSTR) == 0U) continue;
        if (section->size % 4U != 0U) {
            minias_set_error(as, "rv%ui-unaligned-executable-section:%s",
                             as->strict_i_xlen, section->name);
            return false;
        }
        for (offset = 0U; offset < section->size; offset += 4U) {
            const unsigned char *p = section->data + offset;
            uint32_t w = (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
                         ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
            if (!strict_base_i_word(w, as->strict_i_xlen)) {
                minias_set_error(as,
                                 "rv%ui-unsupported-machine-encoding:%s:offset=%zu:word=%08x",
                                 as->strict_i_xlen, section->name, offset, (unsigned)w);
                return false;
            }
        }
    }
    return true;
}


static bool riscv_measure(const MiniAs *as, const char *op, const char *args,
                          uint32_t *size, char *reason, size_t cap) {
    return minias_riscv_measure(as->strict_i_xlen, op, args, size, reason, cap);
}

static bool riscv_target_directive(MiniAs *as, const char *op,
                                   const char *args, size_t line, bool *handled) {
    *handled = false;
    if (as->strict_rv64i && strcmp(op, ".insn") == 0) {
        minias_set_error(as, "unsupported-isa-escape:.insn:line=%zu", line);
        return false;
    }
    if (strcmp(op, ".option") != 0) return true;
    *handled = true;
    if (as->strict_rv64i &&
        strcmp(args, "pic") != 0 && strcmp(args, "nopic") != 0 &&
        strcmp(args, "push") != 0 && strcmp(args, "pop") != 0 &&
        strcmp(args, "norvc") != 0 && strcmp(args, "norelax") != 0 &&
        strcmp(args, "relax") != 0) {
        minias_set_error(as, "unsupported-isa-option:%s:line=%zu", args, line);
        return false;
    }
    /* PIC/nopic must be captured per statement for correct la expansion.
     * Legacy non-I arch/rvc/relax modes still require semantic qualification. */
    if (strcmp(args, "pic") == 0) {
        as->target_options |= MINIAS_RISCV_OPTION_PIC;
    } else if (strcmp(args, "nopic") == 0) {
        as->target_options &= ~(uint32_t)MINIAS_RISCV_OPTION_PIC;
    } else if (strcmp(args, "push") == 0) {
        if (as->target_option_depth >=
            sizeof(as->target_option_stack) /
                sizeof(as->target_option_stack[0])) {
            minias_set_error(as, "option-stack-overflow:line=%zu", line);
            return false;
        }
        as->target_option_stack[as->target_option_depth++] = as->target_options;
    } else if (strcmp(args, "pop") == 0) {
        if (as->target_option_depth == 0U) {
            minias_set_error(as, "option-stack-underflow:line=%zu", line);
            return false;
        }
        as->target_options = as->target_option_stack[--as->target_option_depth];
    }
    return true;
}

/* Target-owned RISC-V architecture attributes. Generic ELF writer does
 * not interpret Tag_RISCV_arch or machine-specific SHT values. */
/* Standard SHT_RISCV_ATTRIBUTES / Tag_RISCV_arch encoding for
 * explicitly qualified RV64I/LP64. A + vendor-length + "riscv" + Tag_File
 * + file-length + ULEB(Tag_arch=5) + NUL-terminated ISA string.
 * The profile is exact rv64i2p1, and thus never advertises extensions the
 * strict-I encoder refuses. Emit nothing for the historical default. */
static const unsigned char rv32i_ilp32_attributes[] = {
    0x41, 0x19, 0x00, 0x00, 0x00,
    'r', 'i', 's', 'c', 'v', 0x00,
    0x01, 0x0f, 0x00, 0x00, 0x00,
    0x05, 'r', 'v', '3', '2', 'i', '2', 'p', '1', 0x00
};
static const unsigned char rv64i_lp64_attributes[] = {
    0x41, 0x19, 0x00, 0x00, 0x00,
    'r', 'i', 's', 'c', 'v', 0x00,
    0x01, 0x0f, 0x00, 0x00, 0x00,
    0x05, 'r', 'v', '6', '4', 'i', '2', 'p', '1', 0x00
};
#define MINIAS_SHT_RISCV_ATTRIBUTES UINT32_C(0x70000003)


static void riscv_object_attributes(const MiniAs *as, unsigned char elf_class,
                                    MiniAsObjectAttribute *out) {
    memset(out, 0, sizeof(*out));
    if ((as->strict_i_xlen == 32U && elf_class == 1U) ||
        (as->strict_i_xlen == 64U && elf_class == 2U)) {
        out->name = ".riscv.attributes";
        out->type = MINIAS_SHT_RISCV_ATTRIBUTES;
        out->data = as->strict_i_xlen == 32U ?
                    rv32i_ilp32_attributes : rv64i_lp64_attributes;
        out->size = as->strict_i_xlen == 32U ?
                    sizeof(rv32i_ilp32_attributes) :
                    sizeof(rv64i_lp64_attributes);
    }
}

static const MiniAsTargetOps riscv_ops = {
    .comment_prefix = '#',
    .elf_machine = 243U, /* EM_RISCV */
    .parse_target_directive = riscv_target_directive,
    .object_attributes = riscv_object_attributes,
    .name = "riscv",
    .measure = riscv_measure,
    .encode = minias_riscv_encode,
    .is_conditional_branch = is_conditional_branch_op,
    .append_code_alignment = minias_section_append_code_alignment,
    .relocation_type = riscv_reloc,
    .validate_output = strict_base_i_check_sections,
};

const MiniAsTargetOps *minias_target_riscv(void) { return &riscv_ops; }
