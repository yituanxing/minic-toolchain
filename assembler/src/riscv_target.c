#include "minias_internal.h"
#include "riscv_target.h"
#include "riscv_profile.h"
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* GNU as fills RISC-V executable .align/.p2align/.balign gaps with
 * executable NOPs, never zero halfwords.  In arch/riscv/lib/strcmp.S the
 * .p2align 3 immediately precedes the aligned Zbb fast path: emitting
 * 0x0000 (C.UNIMP) here traps during early Linux boot even though all ELF
 * sections, exported symbols and relocations pass the basic oracle.
 *
 * Preserve the existing .org zero-fill semantics and data alignment. */
static bool riscv_emit_code_alignment(MiniAs *as,
                                     int section_index,
                                     size_t size) {
    static const unsigned char nop32[] = {0x13U, 0x00U, 0x00U, 0x00U};
    static const unsigned char nop16[] = {0x01U, 0x00U};
    static const unsigned char zeros[16] = {0U};

    if ((size & 1U) != 0U) {
        /* A raw byte in .text (e.g. embedded alternatives/data) can leave
         * an odd-sized alignment gap. No sequence of 16/32-bit RISC-V NOP
         * encodings fills an odd byte count; retain the previous zero-byte
         * fill for this non-instruction-shaped case, as GNU as does. */
        while (size != 0U) {
            size_t chunk = size > sizeof(zeros) ? sizeof(zeros) : size;
            if (!minias_emit_instruction_bytes(as, section_index, zeros, chunk))
                return false;
            size -= chunk;
        }
        return true;
    }
    while (size >= sizeof(nop32)) {
        if (!minias_emit_instruction_bytes(as, section_index,
                                           nop32, sizeof(nop32))) {
            return false;
        }
        size -= sizeof(nop32);
    }
    if (size == sizeof(nop16)) {
        return minias_emit_instruction_bytes(as, section_index,
                                             nop16, sizeof(nop16));
    }
    return true;
}


/* One declarative target ABI inventory is consumed by both semantic
 * capability queries and ELF relocation encoding. A relocation's numeric
 * value must never double as a boolean capability inside Common. */
static const struct {
    MiniAsFixupKind kind;
    unsigned width;
    uint32_t relocation;
} riscv_fixup_forms[] = {
    {MINIAS_FIXUP_ABS, 4U, MINIAS_R_RISCV_32},
    {MINIAS_FIXUP_ABS, 8U, MINIAS_R_RISCV_64},
    {MINIAS_FIXUP_PREL32, 4U, 57U}, /* R_RISCV_32_PCREL */
    {MINIAS_FIXUP_ADD, 2U, MINIAS_R_RISCV_ADD16},
    {MINIAS_FIXUP_ADD, 4U, MINIAS_R_RISCV_ADD32},
    {MINIAS_FIXUP_ADD, 8U, MINIAS_R_RISCV_ADD64},
    {MINIAS_FIXUP_SUB, 2U, MINIAS_R_RISCV_SUB16},
    {MINIAS_FIXUP_SUB, 4U, MINIAS_R_RISCV_SUB32},
    {MINIAS_FIXUP_SUB, 8U, MINIAS_R_RISCV_SUB64},
    {MINIAS_FIXUP_RV_CALL_PLT, 4U, MINIAS_R_RISCV_CALL_PLT},
    {MINIAS_FIXUP_RV_PCREL_HI20, 4U, MINIAS_R_RISCV_PCREL_HI20},
    {MINIAS_FIXUP_RV_GOT_HI20, 4U, MINIAS_R_RISCV_GOT_HI20},
    {MINIAS_FIXUP_RV_PCREL_LO12_I, 4U, MINIAS_R_RISCV_PCREL_LO12_I},
    {MINIAS_FIXUP_RV_PCREL_LO12_S, 4U, MINIAS_R_RISCV_PCREL_LO12_S},
    {MINIAS_FIXUP_RV_JAL, 4U, MINIAS_R_RISCV_JAL}
};

static bool riscv_lookup_fixup_form(MiniAsFixupKind kind, unsigned width,
                                   uint32_t *relocation) {
    size_t i;
    for (i = 0U; i < sizeof(riscv_fixup_forms) / sizeof(riscv_fixup_forms[0]); ++i) {
        if (riscv_fixup_forms[i].kind != kind ||
            riscv_fixup_forms[i].width != width) continue;
        if (relocation != NULL) *relocation = riscv_fixup_forms[i].relocation;
        return true;
    }
    return false;
}

static bool riscv_supports_fixup(MiniAsFixupKind kind, unsigned width) {
    return riscv_lookup_fixup_form(kind, width, NULL);
}

static uint32_t riscv_fixup_relocation_type(MiniAsFixupKind kind,
                                            unsigned width) {
    uint32_t relocation = 0U;
    (void)riscv_lookup_fixup_form(kind, width, &relocation);
    return relocation;
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
    if (MINIAS_RV_STATE(as)->strict_i_xlen == 0U) return true;
    if (MINIAS_RV_STATE(as)->strict_i_xlen != 32U && MINIAS_RV_STATE(as)->strict_i_xlen != 64U) {
        minias_set_error(as, "unsupported-strict-riscv-xlen");
        return false;
    }
    for (i = 0U; i < as->section_count; ++i) {
        const MiniAsSection *section = &as->sections[i];
        size_t offset;
        if ((section->flags & MINIAS_SHF_EXECINSTR) == 0U) continue;
        if (section->size % 4U != 0U) {
            minias_set_error(as, "rv%ui-unaligned-executable-section:%s",
                             MINIAS_RV_STATE(as)->strict_i_xlen, section->name);
            return false;
        }
        for (offset = 0U; offset < section->size; offset += 4U) {
            const unsigned char *p = section->data + offset;
            uint32_t w = (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
                         ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
            if (!strict_base_i_word(w, MINIAS_RV_STATE(as)->strict_i_xlen)) {
                minias_set_error(as,
                                 "rv%ui-unsupported-machine-encoding:%s:offset=%zu:word=%08x",
                                 MINIAS_RV_STATE(as)->strict_i_xlen, section->name, offset, (unsigned)w);
                return false;
            }
        }
    }
    return true;
}



/* The selected ISA is enforced at the target boundary, not in the common
 * assembler. No -march preserves the historical Linux assembler behavior. */
static uint64_t riscv_required_features(const char *op) {
    uint64_t required = 0U;
    if (minias_riscv_mc_descriptor_policy(op, &required, NULL, NULL))
        return required;
    /* Legacy/pseudo and extension families not yet represented by the
     * fixed-width table retain their existing GNU mnemonic policy. */
    if (strcmp(op, "mulhsu") == 0) return RISCV_ISA_M;
    if (strncmp(op, "amo", 3U) == 0 ||
        strncmp(op, "lr.", 3U) == 0 ||
        strncmp(op, "sc.", 3U) == 0) return RISCV_ISA_A;
    if (strncmp(op, "c.", 2U) == 0) return RISCV_ISA_C;
    if (strncmp(op, "vset", 4U) == 0 ||
        strncmp(op, "vle", 3U) == 0 ||
        strncmp(op, "vse", 3U) == 0 ||
        strncmp(op, "vmv.", 4U) == 0) return RISCV_ISA_V;
    if (strncmp(op, "csr", 3U) == 0) return RISCV_ISA_ZICSR;
    if (strcmp(op, "fence.i") == 0) return RISCV_ISA_ZIFENCEI;
    if (strcmp(op, "frcsr") == 0 || strcmp(op, "fscsr") == 0)
        return RISCV_ISA_F | RISCV_ISA_ZICSR;
    if (strcmp(op, "fld") == 0 || strcmp(op, "fsd") == 0 ||
        (op[0] == 'f' && strstr(op, ".d") != NULL))
        return RISCV_ISA_D;
    if (strcmp(op, "flw") == 0 || strcmp(op, "fsw") == 0 ||
        (op[0] == 'f' && (strncmp(op, "fadd", 4U) == 0 ||
         strncmp(op, "fsub", 4U) == 0 ||
         strncmp(op, "fmul", 4U) == 0 ||
         strncmp(op, "fdiv", 4U) == 0 ||
         strncmp(op, "fmv.", 4U) == 0 ||
         strncmp(op, "fsgnj", 5U) == 0 ||
         strncmp(op, "fcvt", 4U) == 0 ||
         strncmp(op, "feq", 3U) == 0 ||
         strncmp(op, "flt", 3U) == 0 ||
         strncmp(op, "fle", 3U) == 0))) return RISCV_ISA_F;
    /* These unprivileged ISA profiles cannot assert implementation of
     * privileged or otherwise unmodelled extension families. */
    if (strcmp(op, "sret") == 0 || strcmp(op, "sfence.vma") == 0 ||
        strcmp(op, "wfi") == 0 || strcmp(op, "pause") == 0)
        return UINT64_MAX;
    return 0U;
}
static bool is_rv64_only_opcode(const char *op) {
    bool rv64_only = false;
    if (minias_riscv_mc_descriptor_policy(op, NULL, &rv64_only, NULL))
        return rv64_only;
    return strcmp(op, "negw") == 0; /* non-table pseudo */
}

static bool riscv_validate_selected_isa(const MiniAs *as, const char *op,
                                        char *reason, size_t cap) {
    uint64_t bits;
    /* Strict base-I retains the exhaustive post-encoding word validator,
     * including its established machine-encoding error diagnostics. */
    if (!MINIAS_RV_STATE(as)->isa_explicit || MINIAS_RV_STATE(as)->strict_i_xlen != 0U) return true;
    if (strcmp(op, ".insn") == 0) {
        (void)snprintf(reason, cap, "unsupported-isa-escape:.insn");
        return false;
    }
    bits = riscv_required_features(op);
    if (((as->target_options & 2U) != 0U) && (bits & RISCV_ISA_C)) {
        (void)snprintf(reason, cap, "disabled-riscv-compressed:%s", op);
        return false;
    }
    if (bits == UINT64_MAX || (bits != 0U &&
        (MINIAS_RV_STATE(as)->isa_features & bits) != bits)) {
        (void)snprintf(reason, cap, "unsupported-isa-feature:%s", op);
        return false;
    }
    return true;
}
static bool riscv_validate_instruction(const MiniAs *as,
                                       const char *op, const char *args,
                                       char *reason, size_t cap) {
    /* Machine feature checks must precede MCInst parsing, but operand
     * decoding and sizing are exclusively the target MCInst hooks. */
    if (!riscv_validate_selected_isa(as, op, reason, cap)) return false;
    if (MINIAS_RV_STATE(as)->isa_explicit && MINIAS_RV_STATE(as)->target_xlen == 32U &&
        is_rv64_only_opcode(op)) {
        (void)snprintf(reason, cap, "riscv32-unsupported-opcode:%s", op);
        return false;
    }
    /* RV32E register bounds are checked after MCInst parsing. Raw tokens
     * such as a6/s2 may name symbols, including branch and LA targets;
     * only typed GPR operands can be interpreted as hardware registers. */
    (void)args;
    return true;
}


/* The common parser must never encode RISC-V ISA escape policy. This
 * backend owns forbidden raw-code directives on an explicit ISA contract.
 * Data outside executable sections remains valid for ELF constants. */
static bool riscv_validate_common_directive(MiniAs *as, const char *op,
                                             size_t line) {
    static const char *const raw_data[] = {
        ".byte", ".half", ".short", ".2byte", ".word", ".long",
        ".4byte", ".dword", ".quad", ".8byte",
        ".zero", ".space", ".asciz", ".string", ".ascii", ".incbin"
    };
    size_t i;
    if (!MINIAS_RV_STATE(as)->strict_rv64i ||
        (as->sections[(size_t)as->current_section].flags &
         MINIAS_SHF_EXECINSTR) == 0U) return true;
    if (strcmp(op, ".org") == 0) {
        minias_set_error(as, "unsupported-isa-escape:.org:line=%zu", line);
        return false;
    }
    for (i = 0U; i < sizeof(raw_data) / sizeof(raw_data[0]); ++i) {
        if (strcmp(op, raw_data[i]) == 0) {
            minias_set_error(as,
                             "unsupported-isa-escape:%s-in-exec:line=%zu",
                             op, line);
            return false;
        }
    }
    return true;
}


/* RISC-V attributes are owned by the ISA target, never by generic ELF.
 * Existing explicit -march provides an ELF attribute subsection containing
 * a single Tag_File+Tag_RISCV_arch. GNU GCC emits stack_align (4) and
 * unaligned_access (6); preserve those numeric attributes exactly and reject
 * conflicting/unsupported attributes instead of silently ignoring them. */
static bool riscv_set_numeric_attribute(MiniAs *as, unsigned tag,
                                        unsigned value, size_t line) {
    unsigned char *buf = MINIAS_RV_STATE(as)->target_attributes;
    size_t n = MINIAS_RV_STATE(as)->target_attributes_size;
    size_t arch, pos;
    uint32_t subsection_size, file_size;
    unsigned i;
    if (value > 127U || n < 18U || buf[0] != 'A' ||
        memcmp(buf + 5, "riscv", 6U) != 0 || buf[11] != 1U) {
        minias_set_error(as, "riscv-attribute-layout-unavailable:line=%zu", line);
        return false;
    }
    arch = buf[16] == 4U ? 18U : 16U;
    if (arch >= n || buf[arch] != 5U) {
        minias_set_error(as, "riscv-attribute-arch-missing:line=%zu", line);
        return false;
    }
    pos = arch + 1U;
    while (pos < n && buf[pos] != 0U) ++pos;
    if (pos >= n) {
        minias_set_error(as, "riscv-attribute-arch-unterminated:line=%zu", line);
        return false;
    }
    ++pos;
    if (tag == 4U && buf[16] == 4U) {
        if (buf[17] != (unsigned char)value) goto conflict;
        return true;
    }
    if (tag == 6U && pos + 2U <= n && buf[pos] == 6U) {
        if (buf[pos + 1U] != (unsigned char)value) goto conflict;
        return true;
    }
    if (n + 2U > sizeof(MINIAS_RV_STATE(as)->target_attributes)) {
        minias_set_error(as, "riscv-attribute-overflow:line=%zu", line);
        return false;
    }
    if (tag == 4U) {
        memmove(buf + 18, buf + 16, n - 16U);
        buf[16] = 4U;
        buf[17] = (unsigned char)value;
    } else if (tag == 6U) {
        if (pos != n) {
            minias_set_error(as, "riscv-attribute-unexpected-tail:line=%zu", line);
            return false;
        }
        buf[n] = 6U;
        buf[n + 1U] = (unsigned char)value;
    } else {
        minias_set_error(as, "unsupported-riscv-attribute-tag:%u:line=%zu",
                         tag, line);
        return false;
    }
    n += 2U;
    MINIAS_RV_STATE(as)->target_attributes_size = n;
    subsection_size = (uint32_t)(n - 1U);
    file_size = (uint32_t)(n - 11U);
    for (i = 0U; i < 4U; ++i) {
        buf[1U + i] = (unsigned char)(subsection_size >> (8U * i));
        buf[12U + i] = (unsigned char)(file_size >> (8U * i));
    }
    return true;
conflict:
    minias_set_error(as, "conflicting-riscv-attribute:%u:line=%zu", tag, line);
    return false;
}

static bool riscv_assemble_attribute(MiniAs *as, const char *args, size_t line) {
    const char *comma = strchr(args, ',');
    char key[48], value[320], *end;
    const char *front, *back;
    size_t len;
    unsigned long number;
    unsigned char *attr = MINIAS_RV_STATE(as)->target_attributes;
    size_t n = MINIAS_RV_STATE(as)->target_attributes_size;
    size_t arch, start, finish;
    if (comma == NULL) goto invalid;
    front = args;
    while (isspace((unsigned char)*front)) ++front;
    len = (size_t)(comma - front);
    while (len && isspace((unsigned char)front[len - 1U])) --len;
    if (!len || len >= sizeof(key)) goto invalid;
    memcpy(key, front, len);
    key[len] = '\0';
    back = comma + 1;
    while (isspace((unsigned char)*back)) ++back;
    len = strlen(back);
    while (len && isspace((unsigned char)back[len - 1U])) --len;
    if (!len || len >= sizeof(value)) goto invalid;
    memcpy(value, back, len);
    value[len] = '\0';

    if (strcmp(key, "arch") == 0 || strcmp(key, "5") == 0) {
        const char *declared = value;
        if (value[0] == '"' && len >= 2U && value[len - 1U] == '"') {
            value[len - 1U] = '\0';
            declared = value + 1;
        }
        if (n < 18U || attr[0] != 'A') goto invalid;
        arch = attr[16] == 4U ? 18U : 16U;
        if (arch >= n || attr[arch] != 5U) goto invalid;
        start = arch + 1U;
        finish = start;
        while (finish < n && attr[finish] != 0U) ++finish;
        if (finish >= n) goto invalid;
        if (strlen(declared) != finish - start ||
            memcmp(attr + start, declared, finish - start) != 0) {
            minias_set_error(as, "riscv-attribute-isa-conflict:%s:line=%zu",
                             declared, line);
            return false;
        }
        return true;
    }
    if (strcmp(key, "stack_align") != 0 && strcmp(key, "4") != 0 &&
        strcmp(key, "unaligned_access") != 0 && strcmp(key, "6") != 0)
        goto invalid;
    number = strtoul(value, &end, 0);
    if (end == value || *end != '\0' || number > 127U)
        goto invalid;
    return riscv_set_numeric_attribute(
        as, (strcmp(key, "stack_align") == 0 || strcmp(key, "4") == 0) ? 4U : 6U,
        (unsigned)number, line);
invalid:
    minias_set_error(as, "unsupported-riscv-attribute:%s:line=%zu", args, line);
    return false;
}

static bool riscv_target_directive(MiniAs *as, const char *op,
                                   const char *args, size_t line, bool *handled) {
    *handled = false;
    if (strcmp(op, ".attribute") == 0) {
        *handled = true;
        return riscv_assemble_attribute(as, args, line);
    }
    if (MINIAS_RV_STATE(as)->strict_rv64i && strcmp(op, ".insn") == 0) {
        minias_set_error(as, "unsupported-isa-escape:.insn:line=%zu", line);
        return false;
    }
    if (strcmp(op, ".option") != 0) return true;
    *handled = true;
    if (MINIAS_RV_STATE(as)->isa_explicit &&
        strcmp(args, "pic") != 0 && strcmp(args, "nopic") != 0 &&
        strcmp(args, "push") != 0 && strcmp(args, "pop") != 0 &&
        strcmp(args, "norvc") != 0) {
        minias_set_error(as, "unsupported-isa-option:%s:line=%zu", args, line);
        return false;
    }
    if (MINIAS_RV_STATE(as)->strict_rv64i &&
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
    } else if (strcmp(args, "norvc") == 0) {
        as->target_options |= 2U; /* Disable explicit C instructions. */
    } else if (strcmp(args, "push") == 0) {
        if (MINIAS_RV_STATE(as)->option_depth >=
            sizeof(MINIAS_RV_STATE(as)->option_stack) /
                sizeof(MINIAS_RV_STATE(as)->option_stack[0])) {
            minias_set_error(as, "option-stack-overflow:line=%zu", line);
            return false;
        }
        MINIAS_RV_STATE(as)->option_stack[MINIAS_RV_STATE(as)->option_depth++] = as->target_options;
    } else if (strcmp(args, "pop") == 0) {
        if (MINIAS_RV_STATE(as)->option_depth == 0U) {
            minias_set_error(as, "option-stack-underflow:line=%zu", line);
            return false;
        }
        as->target_options = MINIAS_RV_STATE(as)->option_stack[--MINIAS_RV_STATE(as)->option_depth];
    }
    return true;
}

/* A selected ISA supplies Tag_RISCV_arch. No -march leaves legacy
 * permissive behavior, so do not invent an ELF ISA attribute. */
static void riscv_object_attributes(const MiniAs *as, unsigned char elf_class,
                                    MiniAsObjectAttribute *out) {
    (void)elf_class;
    memset(out, 0, sizeof(*out));
    if (MINIAS_RV_STATE(as)->target_attributes_size != 0U) {
        out->name = ".riscv.attributes";
        out->type = UINT32_C(0x70000003);
        out->data = MINIAS_RV_STATE(as)->target_attributes;
        out->size = MINIAS_RV_STATE(as)->target_attributes_size;
    }
}

/* One retained MCInst determines both the range-dependent minimum and the
 * terminal encoding alternative. No opcode text is inspected during
 * relaxation: GNU aliases and macro-generated branches behave identically.
 * The inverse B-branch + JAL sequence belongs to the RV encoder;
 * Common only applies the requested fragment size. */
static bool riscv_plan_instruction_relaxation(MiniAs *as,
                                              const MiniAsStmt *stmt,
                                              MiniAsTargetRelaxPlan *plan) {
    MiniAsSymbolExpr expr;
    const MiniAsSymbol *symbol;
    int64_t displacement;
    plan->current_size = stmt->size;
    plan->required_size = stmt->size;
    plan->terminal_size = stmt->size;
    if (stmt->size != 4U || stmt->mcinst == NULL ||
        stmt->mcinst->opcode != RV_MC_BRANCH)
        return true;
    plan->terminal_size = 8U;
    expr = *stmt->mcinst->operands[2].symbol;
    symbol = minias_get_symbol(as, expr.name, false);
    if (symbol == NULL || !symbol->defined ||
        symbol->section != stmt->section ||
        symbol->value > (uint64_t)INT64_MAX ||
        stmt->offset > (uint64_t)INT64_MAX) return true;

    /* Subtract first, then add the symbolic addend with explicit overflow
     * checks. The old expression evaluated (value + addend) before
     * subtraction and could trigger signed overflow for valid offsets. */
    displacement = (int64_t)symbol->value - (int64_t)stmt->offset;
    if ((expr.addend > 0 && displacement > INT64_MAX - expr.addend) ||
        (expr.addend < 0 && displacement < INT64_MIN - expr.addend)) {
        minias_set_error(as, "riscv-relaxation-offset-overflow:%s:line=%zu",
                         stmt->op, stmt->line);
        return false;
    }
    displacement += expr.addend;
    if ((displacement % 2) == 0 &&
        (displacement < -4096 || displacement > 4094)) {
        plan->required_size = 8U;
    }
    return true;
}

static bool riscv_resolve_profile(const char *march, const char *mabi,
                                  MiniAsAssemblyConfig *config,
                                  char *error, size_t capacity) {
    MiniAsRiscvProfile profile;
    if (!minias_riscv_resolve_profile(march, mabi, &profile,
                                     error, capacity)) {
        return false;
    }
    memset(config, 0, sizeof(*config));
    config->target = minias_target_riscv();
    config->elf32 = profile.elf32;
    config->elf_flags = profile.elf_flags;
    if (sizeof(profile) > sizeof(config->target_profile)) {
        (void)snprintf(error, capacity, "riscv-target-profile-overflow");
        return false;
    }
    memcpy(config->target_profile, &profile, sizeof(profile));
    config->target_profile_size = sizeof(profile);
    return true;
}

/* An opaque config blob belongs to this target; Common neither interprets
 * ISA features nor stores RISC-V-specific strict-mode or psABI fields. */
static bool riscv_init_target_state(MiniAs *as,
                                    const MiniAsAssemblyConfig *config) {
    MiniAsRiscvState *state;
    MiniAsRiscvProfile profile;
    if (config->target_profile_size != 0U &&
        config->target_profile_size != sizeof(profile)) {
        minias_set_error(as, "invalid-riscv-target-profile");
        return false;
    }
    state = calloc(1U, sizeof(*state));
    if (state == NULL) {
        minias_set_error(as, "out-of-memory:riscv-target-context");
        return false;
    }
    state->target_xlen = config->elf32 ? 32U : 64U;
    if (config->target_profile_size != 0U) {
        memcpy(&profile, config->target_profile, sizeof(profile));
        if (profile.elf32 != config->elf32 ||
            profile.arch_attribute_size > sizeof(state->target_attributes)) {
            free(state);
            minias_set_error(as, "riscv-target-profile-class-or-attributes");
            return false;
        }
        state->strict_i_xlen = profile.strict_i_xlen;
        state->isa_explicit = profile.explicit_isa;
        state->isa_features = profile.features;
        state->target_attributes_size = profile.arch_attribute_size;
        memcpy(state->target_attributes, profile.arch_attribute,
               profile.arch_attribute_size);
    }
    state->strict_rv64i = state->strict_i_xlen != 0U || state->isa_explicit;
    as->target_state = state;
    return true;
}

static void riscv_destroy_target_state(MiniAs *as) {
    free(as->target_state);
    as->target_state = NULL;
}

static bool riscv_configure_legacy(MiniAsAssemblyConfig *config,
                                   unsigned strict_i_xlen) {
    MiniAsRiscvProfile profile;
    if (strict_i_xlen != 32U && strict_i_xlen != 64U) return false;
    if (config->elf32 != (strict_i_xlen == 32U)) return false;
    memset(&profile, 0, sizeof(profile));
    profile.elf32 = config->elf32;
    profile.strict_i_xlen = strict_i_xlen;
    if (sizeof(profile) > sizeof(config->target_profile)) return false;
    memcpy(config->target_profile, &profile, sizeof(profile));
    config->target_profile_size = sizeof(profile);
    return true;
}

static void riscv_capture_site_state(const MiniAs *as,
                                      MiniAsTargetSiteState *site) {
    site->mode_bits = as->target_options;
    site->profile_features = MINIAS_RV_STATE(as)->isa_features;
}

static void riscv_resolve_cfi_abi(const MiniAs *as, MiniAsCfiABI *abi) {
    const MiniAsRiscvState *state=MINIAS_RV_STATE(as);
    if(state!=NULL && state->target_xlen==32U) {
        abi->address_size=4U;
        abi->data_align=-4; /* RV32 psABI 32-bit saved slots */
    }
}

static const MiniAsTargetOps riscv_ops = {
    .comment_prefix = '#',
    .init_target_state = riscv_init_target_state,
    .destroy_target_state = riscv_destroy_target_state,
    .configure_legacy_target = riscv_configure_legacy,
    .capture_site_state = riscv_capture_site_state,
    .elf_machine = 243U, /* EM_RISCV */
    .parse_target_directive = riscv_target_directive,
    .validate_common_directive = riscv_validate_common_directive,
    .object_attributes = riscv_object_attributes,
    .name = "riscv",
    .resolve_profile = riscv_resolve_profile,
    .validate_instruction = riscv_validate_instruction,
    .parse_mcinst = minias_riscv_parse_mcinst,
    .measure_mcinst = minias_riscv_measure_mcinst,
    .encode = minias_riscv_encode,
    .plan_instruction_relaxation = riscv_plan_instruction_relaxation,
    .append_code_alignment = riscv_emit_code_alignment,
    .supports_fixup = riscv_supports_fixup,
    .fixup_relocation_type = riscv_fixup_relocation_type,
    .validate_output = strict_base_i_check_sections,
    /* RV64 DWARF register numbering: ra=x1, sp=x2; DW_CFA code unit
     * is one byte, stack slots are eight bytes (LP64). Both RV32/64
     * share this Target but DWARF64 address fields are explicitly gated
     * by the generic Output ELF class check. */
    .cfi_implicit_eh_frame = true,
    .cfi_code_align = 1U,
    .cfi_data_align = -8,
    .cfi_return_reg = 1U,
    .cfi_stack_reg = 2U,
    .cfi_address_size = 8U,
    .resolve_cfi_abi = riscv_resolve_cfi_abi,
};

const MiniAsTargetOps *minias_target_riscv(void) { return &riscv_ops; }
