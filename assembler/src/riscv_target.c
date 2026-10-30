#include "minias_internal.h"
#include "riscv_target.h"
#include "riscv_profile.h"
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
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
    static const unsigned char zero = 0U;
    const MiniAsRiscvState *state = MINIAS_RV_STATE(as);
    const MiniAsStmt *owner;
    bool allow_c;

    if (!as->emitting_fragment || as->emitting_fragment_id >= as->stmt_count ||
        state == NULL) {
        minias_set_error(as, "internal:riscv-alignment-site-missing");
        return false;
    }
    owner = &as->stmts[as->emitting_fragment_id];
    /* GNU GAS treats .option norvc as a source-instruction compression mode.
     * A prior C16 instruction can still leave a 2-byte gap which code
     * alignment must fill with C.NOP after switching to norvc.  The ISA
     * capability and ELF RVC flag are separate from that lexical mode. */
    allow_c = (!state->isa_explicit ||
               (state->isa_features & RISCV_ISA_C) != 0U);

    /* GNU GAS riscv_make_nops emits one non-instruction zero byte for odd
     * alignment, then (if needed) the single C.NOP BEFORE its 32-bit NOPs.
     * Keeping that ordering also preserves exact .text bytes for 6B gaps. */
    if ((size & 1U) != 0U) {
        if (!minias_emit_instruction_bytes(as, section_index, &zero, 1U))
            return false;
        --size;
    }
    /* No 32-bit-only NOP sequence can represent a two-byte remainder.
     * An explicit ISA without C must not silently gain a C instruction;
     * a norvc source mode under a C-capable ISA may still use C.NOP padding
     * according to GNU GAS's riscv_make_nops policy. */
    if ((size & 2U) != 0U) {
        if (!allow_c) {
            minias_set_error(as,"riscv-alignment-requires-rvc:line=%zu",
                             owner->line);
            return false;
        }
        if (!minias_emit_instruction_bytes(as, section_index,
                                           nop16, sizeof(nop16)) ||
            !minias_encode_add_elf_flags(as, 1U)) /* EF_RISCV_RVC */
            return false;
        size -= sizeof(nop16);
    }
    while (size >= sizeof(nop32)) {
        if (!minias_emit_instruction_bytes(as, section_index,
                                           nop32, sizeof(nop32))) {
            return false;
        }
        size -= sizeof(nop32);
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
    {MINIAS_FIXUP_SUB, 1U, MINIAS_R_RISCV_SUB8},
    {MINIAS_FIXUP_SUB, 4U, MINIAS_R_RISCV_SUB32},
    {MINIAS_FIXUP_SUB, 8U, MINIAS_R_RISCV_SUB64},
    {MINIAS_FIXUP_SET, 1U, MINIAS_R_RISCV_SET8},
    {MINIAS_FIXUP_SET, 2U, MINIAS_R_RISCV_SET16},
    {MINIAS_FIXUP_SET, 4U, MINIAS_R_RISCV_SET32},
    {MINIAS_FIXUP_RV_CALL_PLT, 4U, MINIAS_R_RISCV_CALL_PLT},
    {MINIAS_FIXUP_RV_PCREL_HI20, 4U, MINIAS_R_RISCV_PCREL_HI20},
    {MINIAS_FIXUP_RV_GOT_HI20, 4U, MINIAS_R_RISCV_GOT_HI20},
    {MINIAS_FIXUP_RV_PCREL_LO12_I, 4U, MINIAS_R_RISCV_PCREL_LO12_I},
    {MINIAS_FIXUP_RV_PCREL_LO12_S, 4U, MINIAS_R_RISCV_PCREL_LO12_S},
    {MINIAS_FIXUP_RV_JAL, 4U, MINIAS_R_RISCV_JAL},
    {MINIAS_FIXUP_RV_HI20, 4U, MINIAS_R_RISCV_HI20},
    {MINIAS_FIXUP_RV_LO12_I, 4U, MINIAS_R_RISCV_LO12_I},
    {MINIAS_FIXUP_RV_LO12_S, 4U, MINIAS_R_RISCV_LO12_S},
    {MINIAS_FIXUP_RV_BRANCH, 4U, MINIAS_R_RISCV_BRANCH},
    {MINIAS_FIXUP_RV_RVC_BRANCH, 2U, MINIAS_R_RISCV_RVC_BRANCH},
    {MINIAS_FIXUP_RV_RVC_JUMP, 2U, MINIAS_R_RISCV_RVC_JUMP}
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



/* One parsed RvMCDesc or Target-private dynamic Form provides the ISA and
 * XLEN requirements along with the actual MCInst. The Common pre-parse
 * validate_instruction hook must not re-parse the opcode as a second source
 * of truth. Strict base-I retains its independent post-encoding raw-word
 * validator, so no .insn/pseudo/relocation placeholder bypass is possible. */
bool minias_riscv_validate_parsed_isa(MiniAs *as, const MiniAsStmt *stmt,
                                      uint64_t required_features,
                                      bool rv64_only) {
    const MiniAsRiscvState *state=MINIAS_RV_STATE(as);
    const char *op=stmt->op;
    /* `.option norvc` is an instruction-site state even in the historical
     * implicit ISA mode. Do not bury the C opcode gate behind explicit
     * -march validation: GNU rejects c.* after norvc in either mode. */
    if (required_features != UINT64_MAX &&
        ((stmt->site.mode_bits & MINIAS_RISCV_OPTION_NO_RVC) != 0U) &&
        (required_features & RISCV_ISA_C)) {
        minias_set_error(as,"disabled-riscv-compressed:%s:line=%zu",
                         op,stmt->line);
        return false;
    }
    /* The .insn directive is rejected by parse_target_directive for explicit
     * ISA contracts before this instruction is ever parsed. */
    /* Privileged instructions are a privilege-level contract, not a
     * -march extension letter. GNU AS accepts sfence.vma/sret/wfi in Linux
     * assembly with rv64imac; target feature checks still apply to ordinary
     * ISA instructions. Strict Base-I audits the encoded words separately. */
    if (state->isa_explicit && state->strict_i_xlen == 0U &&
        required_features != UINT64_MAX && required_features != 0U &&
        (state->isa_features & required_features) != required_features) {
        minias_set_error(as,"unsupported-isa-feature:%s:line=%zu",
                         op,stmt->line);
        return false;
    }
    if (state->target_xlen == 32U && rv64_only) {
        minias_set_error(as,"riscv32-unsupported-opcode:%s:line=%zu",
                         op,stmt->line);
        return false;
    }
    return true;
}


/* The common parser must never encode RISC-V ISA escape policy. The strict
 * Base-I contract forbids unknown executable bytes: a caller claiming an
 * RV32I/RV64I-only object must not bypass instruction checking with raw data.
 * Other explicit -march profiles retain GNU GAS's data-directive semantics,
 * including Linux's inline `.4byte` instruction words in executable sections.
 * ISA-feature checks for typed mnemonics remain independent of this policy. */
static bool riscv_validate_common_directive(MiniAs *as,
                                             MiniAsCommonDirectiveKind kind,
                                             const char *op,size_t line) {
    if (!MINIAS_RV_STATE(as)->reject_raw_exec_fragments ||
        (as->sections[(size_t)as->current_section].flags &
         MINIAS_SHF_EXECINSTR) == 0U) return true;
    if (kind == MINIAS_COMMON_DIRECTIVE_ORG) {
        minias_set_error(as, "unsupported-isa-escape:.org:line=%zu", line);
        return false;
    }
    if (kind == MINIAS_COMMON_DIRECTIVE_RAW_DATA) {
        minias_set_error(as,
                         "unsupported-isa-escape:%s-in-exec:line=%zu",
                         op,line);
        return false;
    }
    return true;
}


/* The TargetContext stores Tag_File attributes as semantic fields. The ELF
 * attribute blob is derived from them, never edited in place using offsets
 * that depend on which source directive happened to come first. GNU emits
 * stack_align (4) before arch (5), then unaligned_access (6). Numeric
 * attributes are ULEB128; treating them as bytes truncates valid values. */
static void riscv_store_le32(unsigned char *out, uint32_t value) {
    unsigned i;
    for (i = 0U; i != 4U; ++i) out[i] = (unsigned char)(value >> (8U * i));
}

static size_t riscv_uleb32_size(unsigned value) {
    size_t size = 1U;
    while (value >= 128U) {
        value >>= 7U;
        ++size;
    }
    return size;
}

static size_t riscv_store_uleb32(unsigned char *out, unsigned value) {
    size_t size = 0U;
    do {
        unsigned char next = (unsigned char)(value & 127U);
        value >>= 7U;
        if (value != 0U) next |= 128U;
        out[size++] = next;
    } while (value != 0U);
    return size;
}

/* Both the ELF architecture attribute and GNU instruction mapping symbols
 * describe the same canonical ISA. Keep its implied-feature expansion in
 * one Target-private place rather than inventing two spellings. */
static bool riscv_canonical_object_isa(const MiniAsRiscvState *state,
                                       char output[512], size_t *length) {
    const char implied_mul[] = "_zmmul1p0";
    size_t source_length;
    bool append_mul;
    if (state == NULL || output == NULL || length == NULL) return false;
    source_length = strlen(state->arch_attribute_isa);
    append_mul = source_length != 0U &&
        (state->isa_features & RISCV_ISA_M) != 0U &&
        strstr(state->arch_attribute_isa, "_zmmul") == NULL;
    *length = source_length +
        (append_mul ? sizeof(implied_mul) - 1U : 0U);
    if (*length >= 512U) return false;
    memcpy(output, state->arch_attribute_isa, source_length);
    if (append_mul)
        memcpy(output + source_length, implied_mul,
               sizeof(implied_mul) - 1U);
    output[*length] = '\0';
    return true;
}

static bool riscv_rebuild_attributes(MiniAs *as, size_t line) {
    MiniAsRiscvState *state = MINIAS_RV_STATE(as);
    /* GAS canonicalizes M as also declaring its implied Zmmul subset even
     * when GCC's source .attribute arch omits that extension. The source
     * contract stays verbatim for .attribute conflict checks; only the
     * serialized Tag_RISCV_arch receives this implied feature. */
    char object_isa[512];
    size_t object_isa_length;
    size_t arch_size;
    /* GNU omits an explicitly declared zero default access mode. */
    const bool emit_unaligned = state->unaligned_access_present &&
                                state->unaligned_access != 0U;
    size_t size;
    unsigned char *buf = state->target_attributes;
    size_t at = 16U;
    if (!riscv_canonical_object_isa(state,object_isa,
                                    &object_isa_length)) {
        minias_set_error(as, "riscv-object-isa-overflow:line=%zu", line);
        return false;
    }
    arch_size = object_isa_length + 1U;
    size = 17U + arch_size +
                        (state->stack_align_present ?
                            1U + riscv_uleb32_size(state->stack_align) : 0U) +
                        (emit_unaligned ?
                            1U + riscv_uleb32_size(state->unaligned_access) : 0U);
    if (size > sizeof(state->target_attributes)) {
        minias_set_error(as, "riscv-attribute-overflow:line=%zu", line);
        return false;
    }
    memset(buf, 0, size);
    buf[0] = 'A';
    riscv_store_le32(buf + 1U, (uint32_t)(size - 1U));
    memcpy(buf + 5U, "riscv", 6U); /* NUL-terminated vendor name */
    buf[11] = 1U; /* Tag_File */
    riscv_store_le32(buf + 12U, (uint32_t)(size - 11U));
    if (state->stack_align_present) {
        buf[at++] = 4U;
        at += riscv_store_uleb32(buf + at, state->stack_align);
    }
    buf[at++] = 5U;
    memcpy(buf + at, object_isa, arch_size);
    at += arch_size;
    if (emit_unaligned) {
        buf[at++] = 6U;
        at += riscv_store_uleb32(buf + at, state->unaligned_access);
    }
    if (at != size) {
        minias_set_error(as, "internal:riscv-attribute-layout");
        return false;
    }
    state->target_attributes_size = size;
    return true;
}

static bool riscv_set_numeric_attribute(MiniAs *as, unsigned tag,
                                        unsigned value, size_t line) {
    MiniAsRiscvState *state = MINIAS_RV_STATE(as);
    bool *present = NULL;
    unsigned *stored = NULL;
    if (state->target_attributes_size == 0U) {
        minias_set_error(as, "riscv-attribute-layout-unavailable:line=%zu", line);
        return false;
    }
    if (tag == 4U) {
        present = &state->stack_align_present;
        stored = &state->stack_align;
    } else if (tag == 6U) {
        present = &state->unaligned_access_present;
        stored = &state->unaligned_access;
    } else {
        minias_set_error(as, "unsupported-riscv-attribute-tag:%u:line=%zu",
                         tag, line);
        return false;
    }
    if (*present) {
        if (*stored != value) {
            minias_set_error(as, "conflicting-riscv-attribute:%u:line=%zu",
                             tag, line);
            return false;
        }
        return true;
    }
    *stored = value;
    *present = true;
    return riscv_rebuild_attributes(as, line);
}

static bool riscv_assemble_attribute(MiniAs *as, const char *args, size_t line) {
    const char *comma = strchr(args, ',');
    char key[48], value[320], *end;
    const char *front, *back;
    size_t len;
    unsigned long number;
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
        if (MINIAS_RV_STATE(as)->target_attributes_size == 0U) goto invalid;
        if (strcmp(declared, MINIAS_RV_STATE(as)->arch_attribute_isa) != 0) {
            minias_set_error(as, "riscv-attribute-isa-conflict:%s:line=%zu",
                             declared, line);
            return false;
        }
        return true;
    }
    if (strcmp(key, "stack_align") != 0 && strcmp(key, "4") != 0 &&
        strcmp(key, "unaligned_access") != 0 && strcmp(key, "6") != 0)
        goto invalid;
    errno = 0;
    number = strtoul(value, &end, 0);
    if (end == value || *end != '\0' || errno == ERANGE ||
        number > UINT32_MAX)
        goto invalid;
    return riscv_set_numeric_attribute(
        as, (strcmp(key, "stack_align") == 0 || strcmp(key, "4") == 0) ? 4U : 6U,
        (unsigned)number, line);
invalid:
    minias_set_error(as, "unsupported-riscv-attribute:%s:line=%zu", args, line);
    return false;
}

/* One option-state transition function for both legacy and explicit ISA
 * modes. Unsupported modifiers are errors, not ignored compatibility input:
 * in particular GNU .option relax needs R_RISCV_RELAX / R_RISCV_ALIGN
 * relocation contracts that this Target has not implemented. */
static bool riscv_target_directive(MiniAs *as, const char *op,
                                   const char *args, size_t line, bool *handled) {
    MiniAsRiscvState *state=MINIAS_RV_STATE(as);
    *handled=false;
    if(strcmp(op,".attribute")==0) {
        *handled=true;
        return riscv_assemble_attribute(as,args,line);
    }
    if(state->reject_raw_exec_fragments && strcmp(op,".insn")==0) {
        minias_set_error(as,"unsupported-isa-escape:.insn:line=%zu",line);
        return false;
    }
    if(strcmp(op,".option")!=0)return true;
    *handled=true;
    if(strcmp(args,"pic")==0)
        state->options|=MINIAS_RISCV_OPTION_PIC;
    else if(strcmp(args,"nopic")==0)
        state->options&=~(uint32_t)MINIAS_RISCV_OPTION_PIC;
    else if(strcmp(args,"norvc")==0)
        state->options|=MINIAS_RISCV_OPTION_NO_RVC;
    else if(strcmp(args,"rvc")==0) {
        /* Restoring C cannot add an extension absent from the ISA. */
        if(state->isa_explicit && !(state->isa_features&RISCV_ISA_C))
            goto unsupported;
        state->options&=~(uint32_t)MINIAS_RISCV_OPTION_NO_RVC;
    } else if(strcmp(args,"push")==0) {
        if(state->option_depth>=sizeof(state->option_stack)/
                                sizeof(state->option_stack[0])) {
            minias_set_error(as,"option-stack-overflow:line=%zu",line);
            return false;
        }
        state->option_stack[state->option_depth++]=state->options;
    } else if(strcmp(args,"pop")==0) {
        if(state->option_depth==0U) {
            minias_set_error(as,"option-stack-underflow:line=%zu",line);
            return false;
        }
        state->options=state->option_stack[--state->option_depth];
    } else if(!state->isa_explicit && strcmp(args,"arch,+v")==0) {
        /* In the permissive legacy profile V is already available. Keep
         * the GNU source spelling without claiming a new .option arch
         * capability; explicit ISA changes still require real snapshots. */
    } else if(strcmp(args,"norelax")!=0) {
        /* Absence of relaxation markers is a real supported norelax mode.
         * A positive relax request cannot silently produce nonrelaxable
         * CALL/HI20/ALIGN relocations. */
        goto unsupported;
    }
    return true;
unsupported:
    minias_set_error(as,"unsupported-isa-option:%s:line=%zu",args,line);
    return false;
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

/* Resolve one retained Target branch against the CURRENT layout generation.
 * Common owns stable parse-time SymbolNameIds and current Object SymbolIDs;
 * RISC-V owns branch encoding policy and may not cache a displacement in
 * MCInst across Common relaxation passes. Growth and emission share the
 * exact same range/alignment decisions. */
bool minias_riscv_resolve_branch_plan(MiniAs *as,
                                      const MiniAsStmt *stmt,
                                      MiniAsRiscvBranchPlan *result) {
    const MiniAsMCInst *m = stmt->mcinst;
    const MiniAsSymbolRef *expr;
    const MiniAsSymbol *symbol;
    int64_t displacement;
    if (result == NULL || m == NULL ||
        (m->opcode != RV_MC_BRANCH && m->opcode != RV_MC_JAL)) {
        minias_set_error(as,"internal:riscv-branch-plan-kind");
        return false;
    }
    memset(result,0,sizeof(*result));
    result->required_size=4U;
    expr=m->operands[m->opcode==RV_MC_BRANCH?2U:1U].bound_symbol;
    if (expr == NULL) {
        minias_set_error(as,"internal:riscv-branch-plan-symbol");
        return false;
    }
    symbol=minias_symbol_ref_lookup(as,*expr);
    if(symbol==NULL || !symbol->defined || symbol->section!=stmt->section ||
       (m->opcode==RV_MC_JAL && symbol->subsection!=stmt->subsection))
        return true;
    if(symbol->value>(uint64_t)INT64_MAX || stmt->offset>(uint64_t)INT64_MAX) {
        minias_set_error(as,"riscv-branch-offset-overflow");
        return false;
    }
    displacement=(int64_t)symbol->value-(int64_t)stmt->offset;
    if((expr->addend>0 && displacement>INT64_MAX-expr->addend) ||
       (expr->addend<0 && displacement<INT64_MIN-expr->addend)) {
        minias_set_error(as,"riscv-branch-addend-overflow");
        return false;
    }
    displacement+=expr->addend;
    result->local=true;
    result->displacement=displacement;
    result->jal_legal=(displacement&1)==0 &&
                      displacement>=-(INT64_C(1)<<20) &&
                      displacement<=(INT64_C(1)<<20)-2;
    if(m->opcode==RV_MC_BRANCH) {
        result->short_legal=(displacement&1)==0 &&
                            displacement>=-4096 && displacement<=4094;
        result->long_legal=displacement>=INT64_MIN+4 &&
                           ((displacement-4)&1)==0 &&
                           displacement-4>=-(INT64_C(1)<<20) &&
                           displacement-4<=(INT64_C(1)<<20)-2;
        /* Odd displacements cannot be repaired by growing the branch.
         * Even overflows grow monotonically; unreachably far targets then
         * fail in final encoding with the established long-JAL diagnostic. */
        if(!result->short_legal && (displacement&1)==0)
            result->required_size=8U;
    }
    return true;
}

/* One retained MCInst determines the range-dependent minimum and terminal
 * encoding alternative.  Common only applies the requested fragment size. */
static bool riscv_plan_instruction_relaxation(MiniAs *as,
                                              const MiniAsStmt *stmt,
                                              MiniAsTargetRelaxPlan *plan) {
    MiniAsRiscvBranchPlan branch;
    plan->current_size = stmt->size;
    plan->required_size = stmt->size;
    plan->terminal_size = stmt->size;
    if (stmt->size == 2U && stmt->mcinst != NULL &&
        (stmt->mcinst->opcode == RV_MC_BRANCH ||
         stmt->mcinst->opcode == RV_MC_JAL)) {
        plan->terminal_size = 4U;
        if (!minias_riscv_resolve_branch_plan(as,stmt,&branch))return false;
        if (branch.local &&
            (((branch.displacement & 1) != 0) ||
             (stmt->mcinst->opcode == RV_MC_JAL ?
                (branch.displacement < -2048 ||
                 branch.displacement > 2046) :
                (branch.displacement < -256 ||
                 branch.displacement > 254))))
            plan->required_size = 4U;
        return true;
    }
    if (stmt->size != 4U || stmt->mcinst == NULL ||
        stmt->mcinst->opcode != RV_MC_BRANCH)
        return true;
    plan->terminal_size = 8U;
    if (!minias_riscv_resolve_branch_plan(as,stmt,&branch))return false;
    plan->required_size=branch.required_size;
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

/* GAS records the effective ISA on its first instruction in executable
 * sections, rather than treating a .riscv.attributes blob as a substitute
 * for local `$x<isa>` mapping symbols. Common serializes this ordinary
 * Target-authored symbol, preserving the final MCLayout section offsets. */
static bool riscv_record_object_mapping_symbols(MiniAs *as) {
    const MiniAsRiscvState *state = MINIAS_RV_STATE(as);
    char canonical[512], name[576];
    size_t isa_length, section_index;
    if (state == NULL || state->arch_attribute_isa[0] == '\0') return true;
    if (!riscv_canonical_object_isa(state, canonical, &isa_length)) {
        minias_set_error(as,"riscv-mapping-isa-overflow");
        return false;
    }
    for (section_index = 0U; section_index < as->section_count;
         ++section_index) {
        MiniAsSymbol *symbol;
        const MiniAsSection *section = &as->sections[section_index];
        size_t ordinal = section_index;
        if ((section->flags & MINIAS_SHF_EXECINSTR) == 0U ||
            section->type != MINIAS_SHT_PROGBITS || section->size == 0U)
            continue;
        for (;;) {
            int written = snprintf(name,sizeof(name),
                                   ordinal==0U ? "$x%s" : "$x%s.%zu",
                                   canonical,ordinal);
            if (written < 0 || (size_t)written >= sizeof(name)) {
                minias_set_error(as,"riscv-mapping-symbol-overflow");
                return false;
            }
            if (minias_get_symbol(as,name,false) == NULL) break;
            if (ordinal == SIZE_MAX) return false;
            ++ordinal;
        }
        symbol = minias_get_symbol(as,name,true);
        if (symbol == NULL) return false;
        symbol->defined = true;
        symbol->section = (int)section_index;
        symbol->value = 0U;
        symbol->bind = MINIAS_STB_LOCAL;
        symbol->type = MINIAS_STT_NOTYPE;
    }
    return true;
}

static bool riscv_validate_output(MiniAs *as) {
    return strict_base_i_check_sections(as) &&
           riscv_record_object_mapping_symbols(as);
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
            memchr(profile.arch_attribute_isa, '\0',
                   sizeof(profile.arch_attribute_isa)) == NULL) {
            free(state);
            minias_set_error(as, "riscv-target-profile-class-or-isa");
            return false;
        }
        state->strict_i_xlen = profile.strict_i_xlen;
        state->isa_explicit = profile.explicit_isa;
        state->isa_features = profile.features;
        /* Profile resolution passes semantic ISA spelling. Only the Target
         * serializes ELF Tag_File attributes, including later directives. */
        memcpy(state->arch_attribute_isa, profile.arch_attribute_isa,
               sizeof(profile.arch_attribute_isa));
        as->target_state = state;
        if (profile.arch_attribute_isa[0] != '\0' &&
            !riscv_rebuild_attributes(as, 0U)) {
            free(state);
            as->target_state = NULL;
            return false;
        }
    }
    /* -march selects permitted *parsed* instruction features. A stronger
     * raw-byte safety guarantee is warranted only for Base-I certification;
     * treating every GNU -march as a raw-data ban rejects valid GCC Linux
     * assembly such as the vDSO's `.4byte 0x0100000f`. */
    state->reject_raw_exec_fragments = state->strict_i_xlen != 0U;
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
    site->mode_bits = MINIAS_RV_STATE(as)->options;
}

static unsigned riscv_default_exec_alignment_for_site(const MiniAs *as) {
    const MiniAsRiscvState *state = MINIAS_RV_STATE(as);
    if (state != NULL && state->isa_explicit &&
        (state->isa_features & RISCV_ISA_C) != 0U &&
        (state->options & MINIAS_RISCV_OPTION_NO_RVC) == 0U)
        return 2U;
    return 4U;
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
    .default_exec_alignment_for_site = riscv_default_exec_alignment_for_site,
    .elf_machine = 243U, /* EM_RISCV */
    .parse_target_directive = riscv_target_directive,
    .validate_common_directive = riscv_validate_common_directive,
    .object_attributes = riscv_object_attributes,
    .name = "riscv",
    .resolve_profile = riscv_resolve_profile,
    .parse_mcinst = minias_riscv_parse_mcinst,
    .measure_mcinst = minias_riscv_measure_mcinst,
    .encode = minias_riscv_encode,
    .plan_instruction_relaxation = riscv_plan_instruction_relaxation,
    .append_code_alignment = riscv_emit_code_alignment,
    .supports_fixup = riscv_supports_fixup,
    .fixup_relocation_type = riscv_fixup_relocation_type,
    .validate_output = riscv_validate_output,
    /* RV64 DWARF register numbering: ra=x1, sp=x2; DW_CFA code unit
     * is one byte, stack slots are eight bytes (LP64). Both RV32/64
     * share this Target but DWARF64 address fields are explicitly gated
     * by the generic Output ELF class check. */
    .cfi_implicit_eh_frame = true,
    .cfi_code_align = 1U,
    .cfi_data_align = -4,
    .cfi_return_reg = 1U,
    .cfi_stack_reg = 2U,
    .cfi_address_size = 8U,
    .cfi_eh_cie_version = 3U,
    .cfi_initial_cfa_register_only = true,
    .cfi_eh_range_relocations = true,
    .cfi_advance_reloc_mode = 2U,
    .emit_all_section_symbols = true,
    .resolve_cfi_abi = riscv_resolve_cfi_abi,
};

const MiniAsTargetOps *minias_target_riscv(void) { return &riscv_ops; }
