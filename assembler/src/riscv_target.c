#include "minias_target_api.h"
#include "riscv_target.h"
#include "riscv_profile.h"
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
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
static bool riscv_emit_code_alignment(MiniAsCodeAlignSink *sink,
                                     const MiniAsCodeAlignmentView *owner,size_t size) {
    static const unsigned char nop32[] = {0x13U, 0x00U, 0x00U, 0x00U};
    static const unsigned char nop16[] = {0x01U, 0x00U};
    static const unsigned char zero = 0U;
    const MiniAsRiscvState *state = minias_code_align_target_state(sink);
    bool allow_c;

    if (!minias_code_align_has_active_fragment(sink) ||
        state == NULL) {
        minias_code_align_error(sink, "internal:riscv-alignment-site-missing");
        return false;
    }
    /* GNU GAS treats .option norvc as a source-instruction compression mode.
     * A prior C16 instruction can still leave a 2-byte gap which code
     * alignment must fill with C.NOP after switching to norvc.  The ISA
     * capability and ELF RVC flag are separate from that lexical mode. */
    allow_c = (!state->isa_explicit ||
               (state->file_isa.features & RISCV_ISA_C) != 0U);

    /* GNU GAS riscv_make_nops emits one non-instruction zero byte for odd
     * alignment, then (if needed) the single C.NOP BEFORE its 32-bit NOPs.
     * Keeping that ordering also preserves exact .text bytes for 6B gaps. */
    if ((size & 1U) != 0U) {
        if (!minias_code_align_emit(sink, &zero, 1U))
            return false;
        --size;
    }
    /* No 32-bit-only NOP sequence can represent a two-byte remainder.
     * An explicit ISA without C must not silently gain a C instruction;
     * a norvc source mode under a C-capable ISA may still use C.NOP padding
     * according to GNU GAS's riscv_make_nops policy. */
    if ((size & 2U) != 0U) {
        if (!allow_c) {
            minias_code_align_error(sink,"riscv-alignment-requires-rvc:line=%zu",
                             owner->line);
            return false;
        }
        if (!minias_code_align_emit(sink,
                                           nop16, sizeof(nop16)) ||
            !minias_code_align_add_elf_flags(sink, 1U)) /* EF_RISCV_RVC */
            return false;
        size -= sizeof(nop16);
    }
    while (size >= sizeof(nop32)) {
        if (!minias_code_align_emit(sink,
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
    /* A variable-width ULEB expression is not an integer field. The
     * zero-width semantic key describes relocation ownership; MCLayout
     * has already reserved and emitted the encoded ULEB payload. */
    {MINIAS_FIXUP_LEB_SET, 0U, MINIAS_R_RISCV_SET_ULEB128},
    {MINIAS_FIXUP_LEB_SUB, 0U, MINIAS_R_RISCV_SUB_ULEB128},
    {MINIAS_FIXUP_CFI_SET6, 1U, 53U}, /* R_RISCV_SET6 */
    {MINIAS_FIXUP_CFI_SUB6, 1U, 52U}, /* R_RISCV_SUB6 */
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

static bool riscv_supports_fixup(MiniAsTargetABIView view,MiniAsFixupKind kind,
                                unsigned width) {
    (void)view;
    return riscv_lookup_fixup_form(kind, width, NULL);
}

static uint32_t riscv_fixup_relocation_type(MiniAsTargetABIView view,MiniAsFixupKind kind,
                                            unsigned width) {
    uint32_t relocation = 0U;
    (void)view;
    (void)riscv_lookup_fixup_form(kind, width, &relocation);
    return relocation;
}

/* Explicit Base-I audit only: validate raw 32-bit encodings AFTER macro/ISA
 * pseudo expansion, section padding and relocation placeholders. Unlike
 * ordinary -march ISA legality, this deliberately rejects executable DATA,
 * .insn and .org: every four-byte word must be a real Base-I instruction.
 * It must never activate implicitly for -march=rv32i/rv64i. */
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

static bool strict_base_i_check_sections(const MiniAsPostEmitView *view,
                                         MiniAsPostEmitErrorSink *errors) {
    const MiniAsRiscvState *state =
        minias_post_emit_target_state(view);
    const size_t section_count=minias_post_emit_section_count(view);
    size_t i;
    if (state->strict_i_xlen == 0U) return true;
    if (state->strict_i_xlen != 32U && state->strict_i_xlen != 64U) {
        minias_post_emit_error(errors, "unsupported-strict-riscv-xlen");
        return false;
    }
    for (i = 0U; i < section_count; ++i) {
        MiniAsSectionView snapshot;
        const MiniAsSectionView *section=&snapshot;
        size_t offset;
        if(!minias_post_emit_section(view,i,&snapshot)) {
            minias_post_emit_error(errors,"invalid-riscv-section-view");
            return false;
        }
        if ((section->flags & MINIAS_SHF_EXECINSTR) == 0U) continue;
        if (section->size % 4U != 0U) {
            minias_post_emit_error(errors,
                                   "rv%ui-unaligned-executable-section:%s",
                                   state->strict_i_xlen, section->name);
            return false;
        }
        for (offset = 0U; offset < section->size; offset += 4U) {
            const unsigned char *p = section->data + offset;
            uint32_t w = (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
                         ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
            if (!strict_base_i_word(w, state->strict_i_xlen)) {
                minias_post_emit_error(
                    errors,
                    "rv%ui-unsupported-machine-encoding:%s:offset=%zu:word=%08x",
                    state->strict_i_xlen, section->name, offset, (unsigned)w);
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
bool minias_riscv_validate_parsed_isa(MiniAsParseContext *ctx, const MiniAsParseInstruction *stmt,
                                      uint64_t required_features,
                                      bool rv64_only) {
    const MiniAsRiscvState *state=minias_parse_target_state(ctx);
    const char *op=stmt->op;
    const uint64_t additions =
        ((stmt->site.mode_bits & MINIAS_RISCV_OPTION_SITE_ZBB)!=0U?
         RISCV_ISA_ZBB:0U) |
        ((stmt->site.mode_bits & MINIAS_RISCV_OPTION_SITE_V)!=0U?
         RISCV_ISA_V:0U);
    uint64_t effective_features=minias_riscv_site_features(
        state->file_isa.features,additions,
        (stmt->site.mode_bits&MINIAS_RISCV_OPTION_NO_RVC)!=0U);
    /* `.option norvc` is an instruction-site state even in the historical
     * implicit ISA mode. Do not bury the C opcode gate behind explicit
     * -march validation: GNU rejects c.* after norvc in either mode. */
    if (required_features != UINT64_MAX &&
        ((stmt->site.mode_bits & MINIAS_RISCV_OPTION_NO_RVC) != 0U) &&
        (required_features & RISCV_ISA_C)) {
        minias_parse_error(ctx,"disabled-riscv-compressed:%s:line=%zu",
                         op,stmt->line);
        return false;
    }
    /* A typed ISA selection permits .insn forms like GNU GAS; the explicit
     * Base-I byte audit rejects them earlier in parse_target_directive. */
    /* Privileged instructions are a privilege-level contract, not a
     * -march extension letter. GNU AS accepts sfence.vma/sret/wfi in Linux
     * assembly with rv64imac; target feature checks still apply to ordinary
     * ISA instructions. Strict Base-I audits the encoded words separately. */
    if (state->isa_explicit &&
        required_features != UINT64_MAX && required_features != 0U &&
        (effective_features & required_features) != required_features) {
        minias_parse_error(ctx,"unsupported-isa-feature:%s:line=%zu",
                         op,stmt->line);
        return false;
    }
    if (state->target_xlen == 32U && rv64_only) {
        minias_parse_error(ctx,"riscv32-unsupported-opcode:%s:line=%zu",
                         op,stmt->line);
        return false;
    }
    return true;
}


/* The common parser must never encode RISC-V ISA escape policy. Only an
 * explicitly requested Base-I audit forbids raw executable bytes; ordinary
 * -march profiles retain GNU GAS's DATA/.org semantics. Typed instruction
 * legality is independently checked against the selected ISA features. */
static bool riscv_validate_common_directive(MiniAsDirectiveContext *ctx,
                                             MiniAsCommonDirectiveKind kind,
                                             const char *op,size_t line) {
    const MiniAsRiscvState *state=minias_directive_target_state(ctx);
    if (!state->reject_raw_exec_fragments ||
        !minias_directive_section_executable(ctx)) return true;
    if (kind == MINIAS_COMMON_DIRECTIVE_ORG) {
        minias_directive_set_error(ctx, "unsupported-isa-escape:.org:line=%zu", line);
        return false;
    }
    if (kind == MINIAS_COMMON_DIRECTIVE_RAW_DATA) {
        minias_directive_set_error(ctx,
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

/* One encoder determines both the preflight size and the stored bytes. */
static size_t riscv_encode_uleb32(unsigned char *out, unsigned value) {
    size_t size = 0U;
    do {
        unsigned char next = (unsigned char)(value & 127U);
        value >>= 7U;
        if (value != 0U) next |= 128U;
        if (out != NULL) out[size] = next;
        ++size;
    } while (value != 0U);
    return size;
}

/* File attributes and initial site mappings share the same canonical ISA
 * inventory. The site projection may add lexical extensions; the immutable
 * file projection never does. In particular GNU inserts implied Zmmul next
 * to M, before explicit Z extensions such as Zbb. */
static bool riscv_canonical_object_isa(const MiniAsRiscvState *state,
                                       char output[512], size_t *length) {
    if (state == NULL || output == NULL || length == NULL) return false;
    if(!minias_riscv_mapping_isa(&state->file_isa,
                                 0U,false,output,512U))
        return false;
    *length=strlen(output);
    return true;
}

/* The only ELF attribute serializer: mutable Target semantic fields enter,
 * one complete canonical blob leaves. Common never sees a partly edited
 * attribute payload, and the SourceGraph pre-layout policy uses this same
 * builder as explicit .attribute directives. NULL denotes success. */
static const char *riscv_build_attributes(MiniAsRiscvState *state) {
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
    const bool emit_priv_major = state->priv_spec != 0U;
    const bool emit_priv_minor = state->priv_spec_minor != 0U;
    const bool emit_priv_revision = state->priv_spec_revision != 0U;
    size_t size;
    unsigned char *buf = state->target_attributes;
    size_t at = 16U;
    if (!riscv_canonical_object_isa(state,object_isa,
                                    &object_isa_length)) {
        return "riscv-object-isa-overflow";
    }
    arch_size = object_isa_length + 1U;
    size = 17U + arch_size +
                        (state->stack_align_present ?
                            1U + riscv_encode_uleb32(NULL,state->stack_align) : 0U) +
                        (emit_unaligned ?
                            1U + riscv_encode_uleb32(NULL,state->unaligned_access) : 0U) +
                        (emit_priv_major ?
                            1U + riscv_encode_uleb32(NULL,state->priv_spec) : 0U) +
                        (emit_priv_minor ?
                            1U + riscv_encode_uleb32(NULL,state->priv_spec_minor) : 0U) +
                        (emit_priv_revision ?
                            1U + riscv_encode_uleb32(NULL,state->priv_spec_revision) : 0U);
    if (size > sizeof(state->target_attributes)) {
        return "riscv-attribute-overflow";
    }
    memset(buf, 0, size);
    buf[0] = 'A';
    riscv_store_le32(buf + 1U, (uint32_t)(size - 1U));
    memcpy(buf + 5U, "riscv", 6U); /* NUL-terminated vendor name */
    buf[11] = 1U; /* Tag_File */
    riscv_store_le32(buf + 12U, (uint32_t)(size - 11U));
    if (state->stack_align_present) {
        buf[at++] = 4U;
        at += riscv_encode_uleb32(buf + at, state->stack_align);
    }
    buf[at++] = 5U;
    memcpy(buf + at, object_isa, arch_size);
    at += arch_size;
    if (emit_unaligned) {
        buf[at++] = 6U;
        at += riscv_encode_uleb32(buf + at, state->unaligned_access);
    }
    if (emit_priv_major) {
        buf[at++] = 8U;
        at += riscv_encode_uleb32(buf + at, state->priv_spec);
    }
    if (emit_priv_minor) {
        buf[at++] = 10U;
        at += riscv_encode_uleb32(buf + at, state->priv_spec_minor);
    }
    if (emit_priv_revision) {
        buf[at++] = 12U;
        at += riscv_encode_uleb32(buf + at, state->priv_spec_revision);
    }
    if (at != size) {
        return "internal:riscv-attribute-layout";
    }
    state->target_attributes_size = size;
    return NULL;
}

/* The textual spelling, ELF tag and Target state slot are one attribute
 * identity. Keep name dispatch and duplicate-value checks in agreement. */
typedef struct {
    const char *name;
    const char *tag_name;
    unsigned tag;
    size_t present_offset;
    size_t value_offset;
} RiscvNumericAttribute;

#define RISCV_NUMERIC_ATTRIBUTE(name, number, field) \
    {name, #number, number##U, offsetof(MiniAsRiscvState, field##_present), \
     offsetof(MiniAsRiscvState, field)}
static const RiscvNumericAttribute riscv_numeric_attributes[] = {
    RISCV_NUMERIC_ATTRIBUTE("stack_align", 4, stack_align),
    RISCV_NUMERIC_ATTRIBUTE("unaligned_access", 6, unaligned_access),
    RISCV_NUMERIC_ATTRIBUTE("priv_spec", 8, priv_spec),
    RISCV_NUMERIC_ATTRIBUTE("priv_spec_minor", 10, priv_spec_minor),
    RISCV_NUMERIC_ATTRIBUTE("priv_spec_revision", 12, priv_spec_revision)
};
#undef RISCV_NUMERIC_ATTRIBUTE

static bool riscv_set_numeric_attribute(MiniAsDirectiveContext *ctx,
                                        const RiscvNumericAttribute *attribute,
                                        unsigned value, size_t line) {
    MiniAsRiscvState *state = minias_directive_target_state(ctx);
    bool *present = (bool *)((char *)state + attribute->present_offset);
    unsigned *stored = (unsigned *)((char *)state + attribute->value_offset);
    const char *error;
    if (state->target_attributes_size == 0U) {
        minias_directive_set_error(ctx,
                                   "riscv-attribute-layout-unavailable:line=%zu", line);
        return false;
    }
    if (*present) {
        if (*stored != value) {
            minias_directive_set_error(ctx,
                                       "conflicting-riscv-attribute:%u:line=%zu",
                                       attribute->tag, line);
            return false;
        }
        return true;
    }
    *stored = value;
    *present = true;
    error=riscv_build_attributes(state);
    if(error!=NULL) {
        minias_directive_set_error(ctx,"%s:line=%zu",error,line);
        return false;
    }
    return true;
}

static bool riscv_assemble_attribute(MiniAsDirectiveContext *ctx,
                                     const char *args, size_t line) {
    const char *comma = strchr(args, ',');
    const RiscvNumericAttribute *attribute = NULL;
    const MiniAsRiscvState *state=minias_directive_target_state(ctx);
    char key[48], value[320], *end;
    const char *front, *back;
    size_t len;
    unsigned long number;
    size_t i;
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
        if (state->target_attributes_size == 0U) goto invalid;
        if (strcmp(declared, state->arch_attribute_isa) != 0) {
            minias_directive_set_error(ctx,
                                       "riscv-attribute-isa-conflict:%s:line=%zu",
                                       declared, line);
            return false;
        }
        return true;
    }
    for (i = 0U; i < sizeof(riscv_numeric_attributes) /
                      sizeof(riscv_numeric_attributes[0]); ++i) {
        const RiscvNumericAttribute *candidate = &riscv_numeric_attributes[i];
        if (strcmp(key, candidate->name) == 0 ||
            strcmp(key, candidate->tag_name) == 0) {
            attribute = candidate;
            break;
        }
    }
    if (attribute == NULL) goto invalid;
    /* GNU forbids setting the privileged ELF version after an instruction,
     * including an ordinary NOP. Retained Fragments give us that boundary
     * without rescanning source text or touching Common parser state. */
    if (attribute->tag >= 8U && minias_directive_has_prior_instruction(ctx)) {
        minias_directive_set_error(ctx,
                                   "riscv-priv-attribute-after-instruction:line=%zu",
                                   line);
        return false;
    }
    errno = 0;
    number = strtoul(value, &end, 0);
    if (end == value || *end != '\0' || errno == ERANGE ||
        number > UINT32_MAX)
        goto invalid;
    return riscv_set_numeric_attribute(ctx,attribute,(unsigned)number,line);
invalid:
    minias_directive_set_error(ctx,
                               "unsupported-riscv-attribute:%s:line=%zu", args, line);
    return false;
}

/* GAS marks a file as using the privileged specification for the source CSR
 * instruction family and selected privileged instructions. The *spelling*
 * matters: rdcycle/rdtime aliases, floating CSR aliases, ECALL/EBREAK and
 * literal .insn bytes do not trigger Tag_RISCV_priv_spec even if some encode
 * with the same system opcode. Match only verified, successfully parsed
 * instruction Fragments, never the broad 0x73 encoding or arbitrary text. */
static bool riscv_source_uses_priv_spec(const char *op) {
    static const char *const csr_forms[] = {
        "csrr", "csrrw", "csrrs", "csrrc", "csrrwi", "csrrsi", "csrrci",
        "csrw", "csrs", "csrc", "csrwi", "csrsi", "csrci",
        "sret", "wfi", "sfence.vma"
    };
    size_t i;
    for (i = 0U; i < sizeof(csr_forms) / sizeof(csr_forms[0]); ++i)
        if (strcmp(op,csr_forms[i]) == 0) return true;
    return false;
}

/* Complete parsed instruction graph, but mutable TargetContext only. GNU
 * defaults to privileged version 1.11.0 on an inferred use, unless the
 * source supplied a nonzero, supported version tuple. A requested partial
 * tuple (e.g. 1.0.0) is an error, not permission to fill missing components.
 * The common SourceGraph and MCLayout are only borrowed, never modified. */
static bool riscv_prepare_layout(const MiniAsPreLayoutView *view,
                                 char *error, size_t error_size) {
    MiniAsRiscvState *state = minias_pre_layout_target_state(view);
    bool uses_priv_spec = false;
    size_t i;
    for (i = 0U; i < minias_pre_layout_fragment_count(view); ++i) {
        MiniAsFragmentView fragment;
        if(!minias_pre_layout_fragment(view,i,&fragment)) {
            (void)snprintf(error,error_size,"invalid-riscv-fragment-view");
            return false;
        }
        if (fragment.kind == MINIAS_FRAGMENT_INSTRUCTION &&
            riscv_source_uses_priv_spec(fragment.op)) {
            uses_priv_spec = true;
            break;
        }
    }
    if (state->priv_spec == 0U && state->priv_spec_minor == 0U &&
        state->priv_spec_revision == 0U && uses_priv_spec &&
        state->target_attributes_size != 0U) {
        /* All-zero explicit tags are also GNU's default-selection sentinel. */
        state->priv_spec = 1U;
        state->priv_spec_minor = 11U;
    }
    if (state->priv_spec != 0U || state->priv_spec_minor != 0U ||
        state->priv_spec_revision != 0U) {
        const unsigned major = state->priv_spec;
        const unsigned minor = state->priv_spec_minor;
        const unsigned revision = state->priv_spec_revision;
        if (major != 1U ||
            !((minor == 9U && revision == 1U) ||
              (minor == 10U && revision == 0U) ||
              (minor == 11U && revision == 0U) ||
              (minor == 12U && revision == 0U))) {
            snprintf(error,error_size,
                     "unknown-riscv-priv-spec:%u.%u.%u",major,minor,revision);
            return false;
        }
    }
    if (state->target_attributes_size != 0U) {
        const char *build_error = riscv_build_attributes(state);
        if (build_error != NULL) {
            snprintf(error,error_size,"%s",build_error);
            return false;
        }
    }
    return true;
}

/* Recognize only documented region ISA additions. GNU accepts
 * whitespace around the comma (`arch,+v` and `arch, +v` are equivalent);
 * do not turn other .option arch operands into silent no-ops. The returned
 * feature is NOT a file-wide Tag_RISCV_arch attribute update. */
static uint64_t riscv_arch_add_feature_option(const char *args) {
    const char *p = args;
    const char *start;
    uint64_t feature;
    while (isspace((unsigned char)*p)) ++p;
    if (strncmp(p, "arch", 4U) != 0) return 0U;
    p += 4U;
    while (isspace((unsigned char)*p)) ++p;
    if (*p++ != ',') return 0U;
    while (isspace((unsigned char)*p)) ++p;
    if (*p++ != '+') return 0U;
    start=p;
    while (*p!='\0' && !isspace((unsigned char)*p))++p;
    feature=minias_riscv_lookup_site_extension(start,(size_t)(p-start));
    while (isspace((unsigned char)*p)) ++p;
    return *p=='\0'?feature:0U;
}

/* One option-state transition function for both legacy and explicit ISA
 * modes. Unsupported modifiers are errors, not ignored compatibility input:
 * in particular GNU .option relax needs R_RISCV_RELAX / R_RISCV_ALIGN
 * relocation contracts that this Target has not implemented. */
static bool riscv_target_directive(MiniAsDirectiveContext *ctx, const char *op,
                                   const char *args, size_t line, bool *handled) {
    MiniAsRiscvState *state=minias_directive_target_state(ctx);
    uint64_t region_feature;
    *handled=false;
    if(strcmp(op,".attribute")==0) {
        *handled=true;
        return riscv_assemble_attribute(ctx,args,line);
    }
    if(state->reject_raw_exec_fragments && strcmp(op,".insn")==0) {
        minias_directive_set_error(ctx,"unsupported-isa-escape:.insn:line=%zu",line);
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
        if(state->isa_explicit && !(state->file_isa.features&RISCV_ISA_C))
            goto unsupported;
        state->options&=~(uint32_t)MINIAS_RISCV_OPTION_NO_RVC;
    } else if(strcmp(args,"push")==0) {
        if(state->option_depth>=sizeof(state->option_stack)/
                                sizeof(state->option_stack[0])) {
            minias_directive_set_error(ctx,"option-stack-overflow:line=%zu",line);
            return false;
        }
        state->option_stack[state->option_depth++]=state->options;
    } else if(strcmp(args,"pop")==0) {
        if(state->option_depth==0U) {
            minias_directive_set_error(ctx,"option-stack-underflow:line=%zu",line);
            return false;
        }
        state->options=state->option_stack[--state->option_depth];
    } else if((region_feature=riscv_arch_add_feature_option(args)) != 0U) {
        /* GNU permits +zbb/+v at an instruction site even with explicit
         * -march lacking the feature. push/pop restores these lexical bits;
         * ELF Tag_RISCV_arch still describes the selected file ISA. */
        if (region_feature == RISCV_ISA_ZBB)
            state->options|=MINIAS_RISCV_OPTION_SITE_ZBB;
        else if (region_feature == RISCV_ISA_V)
            state->options|=MINIAS_RISCV_OPTION_SITE_V;
        /* Legacy implicit ISA already permits V and Zbb instructions. */
    } else if(strcmp(args,"norelax")==0) {
        /* GNU GAS retains the final target .option relax state through
         * push/pop and gates deferred ULEB relocation emission with it.
         * Keep the target-owned bit even though this assembler does not
         * yet publish general R_RISCV_RELAX instruction markers. */
        state->options|=MINIAS_RISCV_OPTION_NO_RELAX;
    } else {
        /* A positive relax request cannot silently produce nonrelaxable
         * CALL/HI20/ALIGN relocations. */
        goto unsupported;
    }
    return true;
unsupported:
    minias_directive_set_error(ctx,"unsupported-isa-option:%s:line=%zu",args,line);
    return false;
}

/* A selected ISA supplies Tag_RISCV_arch. No -march leaves legacy
 * permissive behavior, so do not invent an ELF ISA attribute. */
static void riscv_object_attributes(MiniAsTargetABIView view, unsigned char elf_class,
                                    MiniAsObjectAttribute *out) {
    const MiniAsRiscvState *state=view.target_state;
    (void)elf_class;
    memset(out, 0, sizeof(*out));
    if (state->target_attributes_size != 0U) {
        out->name = ".riscv.attributes";
        out->type = UINT32_C(0x70000003);
        out->data = state->target_attributes;
        out->size = state->target_attributes_size;
    }
}

/* One pure target encoding policy, evaluated from a caller-supplied symbol
 * snapshot of the CURRENT Common layout generation. Both relaxation and
 * final emission invoke this identical distance/range algorithm; neither
 * retains the result across MCLayout passes. */
bool minias_riscv_compute_branch_plan(const MiniAsInstructionView *stmt,
                                      const MiniAsSymbolView *symbol,
                                      MiniAsRiscvBranchPlan *result,
                                      const char **diagnostic) {
    const MiniAsMCInstView *m = stmt != NULL ? stmt->inst : NULL;
    const MiniAsSymbolRef *expr;
    bool same_section;
    int64_t displacement;
    if(diagnostic!=NULL)*diagnostic=NULL;
    if (result == NULL || m == NULL ||
        (m->opcode != RV_MC_BRANCH && m->opcode != RV_MC_JAL)) {
        if(diagnostic!=NULL)*diagnostic="internal:riscv-branch-plan-kind";
        return false;
    }
    memset(result,0,sizeof(*result));
    /* A nonlocal B-type target has no relocation that can repair the
     * 12-bit displacement. GAS emits an inverted B+8 followed by JAL+REL.
     * A JAL itself can always carry a linker-visible JAL relocation. */
    result->required_size=m->opcode==RV_MC_BRANCH?8U:4U;
    expr=m->operands[m->opcode==RV_MC_BRANCH?2U:1U].bound_symbol;
    if (expr == NULL) {
        if(diagnostic!=NULL)*diagnostic="internal:riscv-branch-plan-symbol";
        return false;
    }
    same_section=symbol!=NULL && symbol->defined &&
                 symbol->section==stmt->section;
    if(stmt->offset>(uint64_t)INT64_MAX ||
       (same_section && symbol->value>(uint64_t)INT64_MAX)) {
        if(diagnostic!=NULL)*diagnostic="riscv-branch-offset-overflow";
        return false;
    }
    /* GNU writes S+A-P to the relocation field even for a defined WEAK
     * symbol, but the relocation must remain link-time replaceable. An
     * undefined/cross-section symbol uses S=0 as its input-field value. */
    displacement=(same_section?(int64_t)symbol->value:0)-
                 (int64_t)stmt->offset;
    if(!minias_checked_add_i64(displacement,expr->addend,&displacement)) {
        if(diagnostic!=NULL)*diagnostic="riscv-branch-addend-overflow";
        return false;
    }
    result->displacement=displacement;
    result->local=same_section && symbol->bind!=MINIAS_STB_WEAK &&
                  (m->opcode!=RV_MC_JAL ||
                   symbol->subsection==stmt->subsection);
    if(!result->local)return true;
    result->required_size=4U;
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

const MiniAsSymbolRef *minias_riscv_branch_bound_symbol(
    const MiniAsInstructionView *stmt) {
    const MiniAsMCInstView *m;
    if(stmt==NULL)
        return NULL;
    m=stmt->inst;
    if(m==NULL || (m->opcode!=RV_MC_BRANCH && m->opcode!=RV_MC_JAL))
        return NULL;
    return m->operands[m->opcode==RV_MC_BRANCH?2U:1U].bound_symbol;
}

/* Common owns symbol lookup and diagnostic storage in the relaxation phase.
 * This wrapper only projects current symbol metadata into the pure policy. */
static bool riscv_relax_branch_plan(MiniAsRelaxContext *ctx,
                                    const MiniAsInstructionView *stmt,
                                    MiniAsRiscvBranchPlan *result) {
    const MiniAsSymbolRef *expr=minias_riscv_branch_bound_symbol(stmt);
    MiniAsSymbolView symbol_snapshot;
    const MiniAsSymbolView *symbol=expr!=NULL &&
        minias_relax_symbol_ref_lookup(ctx,*expr,&symbol_snapshot)?
        &symbol_snapshot:NULL;
    const char *diagnostic=NULL;
    if(!minias_riscv_compute_branch_plan(stmt,symbol,result,&diagnostic)) {
        minias_relax_error(ctx,"%s",diagnostic!=NULL?diagnostic:
                           "internal:riscv-branch-plan");
        return false;
    }
    return true;
}

/* One retained MCInst determines the range-dependent minimum and terminal
 * encoding alternative.  Common only applies the requested fragment size. */
static bool riscv_plan_instruction_relaxation(MiniAsRelaxContext *ctx,
                                              const MiniAsInstructionView *stmt,
                                              MiniAsTargetRelaxPlan *plan) {
    MiniAsRiscvBranchPlan branch;
    plan->current_size = stmt->size;
    plan->required_size = stmt->size;
    plan->terminal_size = stmt->size;
    if (stmt->size == 2U && stmt->inst != NULL &&
        (stmt->inst->opcode == RV_MC_BRANCH ||
         stmt->inst->opcode == RV_MC_JAL)) {
        plan->terminal_size = 4U;
        if (!riscv_relax_branch_plan(ctx,stmt,&branch))return false;
        if (!branch.local ||
            ((branch.displacement & 1) != 0) ||
            (stmt->inst->opcode == RV_MC_JAL ?
                (branch.displacement < -2048 ||
                 branch.displacement > 2046) :
                (branch.displacement < -256 ||
                 branch.displacement > 254)))
            plan->required_size = 4U;
        return true;
    }
    if (stmt->size != 4U || stmt->inst == NULL ||
        stmt->inst->opcode != RV_MC_BRANCH)
        return true;
    plan->terminal_size = 8U;
    if (!riscv_relax_branch_plan(ctx,stmt,&branch))return false;
    plan->required_size=branch.required_size;
    return true;
}

static bool riscv_resolve_profile(const char *march, const char *mabi,
                                  MiniAsTargetSpec *config,
                                  char *error, size_t capacity) {
    MiniAsRiscvProfile profile;
    if (!minias_riscv_resolve_profile(march, mabi, &profile,
                                     error, capacity)) {
        return false;
    }
    memset(config, 0, sizeof(*config));
    config->target = minias_target_riscv();
    config->object.elf32 = profile.elf32;
    config->object.initial_elf_flags = profile.elf_flags;
    if (sizeof(profile) > sizeof(config->profile)) {
        (void)snprintf(error, capacity, "riscv-target-profile-overflow");
        return false;
    }
    memcpy(config->profile, &profile, sizeof(profile));
    config->profile_size = sizeof(profile);
    return true;
}

/* GNU GAS 2.42 mapping_state is a SOURCE-EVENT state machine per Section,
 * not an offset-sorted stream. A .subsection switch retains the Section's
 * last map/arch state; source-order symbols may therefore have nonmonotonic
 * final values. Their values still come from the one final MCLayout.
 * The Target never owns Section, Symbol or Fragment storage. */
typedef enum RiscvMappingKind {
    RISCV_MAP_NONE,
    RISCV_MAP_DATA,
    RISCV_MAP_CODE
} RiscvMappingKind;

typedef struct RiscvMappingSectionState {
    RiscvMappingKind kind;
    int last_arch_key; /* -1 before the Section's first mapped instruction */
    bool pending_zero_data;
} RiscvMappingSectionState;

static unsigned riscv_mapping_site_key(MiniAsTargetSiteState site) {
    return ((site.mode_bits&MINIAS_RISCV_OPTION_NO_RVC)!=0U?1U:0U) |
           ((site.mode_bits&MINIAS_RISCV_OPTION_SITE_ZBB)!=0U?2U:0U) |
           ((site.mode_bits&MINIAS_RISCV_OPTION_SITE_V)!=0U?4U:0U);
}

static bool riscv_mapping_is_code(const MiniAsFragmentView *fragment) {
    return fragment->kind==MINIAS_FRAGMENT_INSTRUCTION ||
           (fragment->site.provenance_bits&MINIAS_RISCV_SITE_INSN_DATA)!=0U ||
           (fragment->kind==MINIAS_FRAGMENT_ALIGNMENT &&
            !fragment->alignment_has_fill);
}

static bool riscv_record_object_mapping_symbols(
    const MiniAsPostEmitView *view, MiniAsPostEmitErrorSink *errors,
    MiniAsPostEmitObjectSink *object) {
    const MiniAsRiscvState *state =
        minias_post_emit_target_state(view);
    const size_t section_count=minias_post_emit_section_count(view);
    const size_t fragment_count=minias_post_emit_fragment_count(view);
    RiscvMappingSectionState *sections;
    char site_names[8][512]={{0}};
    bool cached[8]={false};
    size_t i;
    bool ok=false;
    if (state == NULL || state->arch_attribute_isa[0] == '\0') return true;
    if(section_count>SIZE_MAX/sizeof(*sections)) {
        minias_post_emit_error(errors,"riscv-mapping-section-overflow");
        return false;
    }
    sections=calloc(section_count==0U?1U:section_count,sizeof(*sections));
    if(sections==NULL) {
        minias_post_emit_error(errors,"out-of-memory:riscv-mapping-sections");
        return false;
    }
    for(i=0U;i<section_count;++i)sections[i].last_arch_key=-1;
    for(i=0U;i<fragment_count;++i) {
        MiniAsFragmentView fragment;
        MiniAsSectionView section;
        RiscvMappingSectionState *previous;
        RiscvMappingKind kind;
        const char *name;
        char arch_name[576];
        unsigned key;
        bool changed;
        if(!minias_post_emit_fragment(view,i,&fragment)) {
            minias_post_emit_error(errors,"invalid-riscv-mapping-fragment");
            goto done;
        }
        if(fragment.section<0 || (size_t)fragment.section>=section_count)
            continue;
        if(!minias_post_emit_section(view,(size_t)fragment.section,&section)) {
            minias_post_emit_error(errors,"invalid-riscv-mapping-section");
            goto done;
        }
        if(section.type!=MINIAS_SHT_PROGBITS ||
           (section.flags&MINIAS_SHF_EXECINSTR)==0U || section.size==0U)
            continue;
        previous=&sections[(size_t)fragment.section];
        kind=riscv_mapping_is_code(&fragment)?RISCV_MAP_CODE:RISCV_MAP_DATA;
        if(fragment.size==0U) {
            /* GAS may pass a zero-width .fill/.space through MAP_DATA.
             * Its transient $d is replaced by the next code symbol at the
             * same position. Retain that transition intent, not a zero-byte
             * output symbol that would survive after the Section ends. */
            if(kind==RISCV_MAP_DATA && previous->kind!=RISCV_MAP_DATA) {
                previous->kind=RISCV_MAP_DATA;
                previous->pending_zero_data=true;
            }
            continue;
        }
        if(kind==RISCV_MAP_DATA) {
            if(previous->kind==RISCV_MAP_DATA &&
               !previous->pending_zero_data)continue;
            name="$d";
        }else {
            const uint64_t additions=
                ((fragment.site.mode_bits&MINIAS_RISCV_OPTION_SITE_ZBB)!=0U?
                 RISCV_ISA_ZBB:0U) |
                ((fragment.site.mode_bits&MINIAS_RISCV_OPTION_SITE_V)!=0U?
                 RISCV_ISA_V:0U);
            key=riscv_mapping_site_key(fragment.site);
            if(!cached[key]) {
                if(!minias_riscv_mapping_isa(&state->file_isa,
                        additions,(key&1U)!=0U,
                        site_names[key],sizeof(site_names[key]))) {
                    minias_post_emit_error(errors,"riscv-mapping-isa-overflow");
                    goto done;
                }
                cached[key]=true;
            }
            changed=previous->last_arch_key<0 ||
                strcmp(site_names[key],site_names[previous->last_arch_key])!=0;
            if(!changed && previous->kind==RISCV_MAP_CODE)continue;
            if(changed) {
                int n=snprintf(arch_name,sizeof(arch_name),"$x%s",site_names[key]);
                if(n<0 || (size_t)n>=sizeof(arch_name)) {
                    minias_post_emit_error(errors,"riscv-mapping-symbol-overflow");
                    goto done;
                }
                name=arch_name;
                previous->last_arch_key=(int)key;
            }else name="$x";
        }
        /* GNU permits repeated, identical LOCAL names. The Common sink
         * appends exact object symbols outside the source-name hash. */
        if(!minias_post_emit_append_exact_mapping_symbol(
                object,name,fragment.section,fragment.offset))goto done;
        previous->kind=kind;
        previous->pending_zero_data=false;
    }
    ok=true;
done:
    free(sections);
    return ok;
}

/* GAS's riscv_md_finish gates every deferred ULEB pair on the final global
 * relaxation mode, not on the individual source site's .option snapshot. */
static bool riscv_leb128_relocations_enabled(MiniAsTargetABIView view) {
    const MiniAsRiscvState *state=view.target_state;
    return state!=NULL &&
           (state->options & MINIAS_RISCV_OPTION_NO_RELAX)==0U;
}

/* GNU RISC-V GAS retains the two source-label identities in the deferred
 * R_RISCV_SET_ULEB128/SUB_ULEB128 pair.  The linker may relax instructions
 * between those anchors; turning either operand into STT_SECTION + addend
 * too early loses the symbol that GNU's relocation refers to.  This is a
 * Target ELF rule, not a generic Common symbol-folding exception. */
static bool riscv_allow_local_section_symbol_fixup(
    const MiniAsFixup *fixup, const MiniAsSymbolView *symbol) {
    (void)symbol;
    return fixup->kind!=MINIAS_FIXUP_LEB_SET &&
           fixup->kind!=MINIAS_FIXUP_LEB_SUB;
}

/* An opaque config blob belongs to this target; Common neither interprets
 * ISA features nor stores RISC-V-specific strict-mode or psABI fields. */
static bool riscv_init_target_state(const MiniAsTargetSpec *config,
                                    void **out,char *error,size_t capacity) {
    MiniAsRiscvState *state;
    MiniAsRiscvProfile profile;
    const char *attribute_error;
    *out=NULL;
    if (config->profile_size != 0U &&
        config->profile_size != sizeof(profile)) {
        if(capacity)snprintf(error,capacity,"invalid-riscv-target-profile");
        return false;
    }
    state = calloc(1U, sizeof(*state));
    if (state == NULL) {
        if(capacity)snprintf(error,capacity,"out-of-memory:riscv-target-context");
        return false;
    }
    state->target_xlen = config->object.elf32 ? 32U : 64U;
    state->rv32e_abi = (config->object.initial_elf_flags & 8U) != 0U; /* EF_RISCV_RVE */
    if (config->profile_size != 0U) {
        memcpy(&profile, config->profile, sizeof(profile));
        if (profile.elf32 != config->object.elf32 ||
            memchr(profile.arch_attribute_isa, '\0',
                   sizeof(profile.arch_attribute_isa)) == NULL) {
            free(state);
            if(capacity)snprintf(error,capacity,"riscv-target-profile-class-or-isa");
            return false;
        }
        state->strict_i_xlen = profile.strict_i_xlen;
        state->isa_explicit = profile.explicit_isa;
        state->file_isa = profile.isa;
        /* Profile resolution passes semantic ISA spelling. Only the Target
         * serializes ELF Tag_File attributes, including later directives. */
        memcpy(state->arch_attribute_isa, profile.arch_attribute_isa,
               sizeof(profile.arch_attribute_isa));
        if (profile.arch_attribute_isa[0] != '\0') {
            attribute_error=riscv_build_attributes(state);
            if(attribute_error!=NULL) {
                if(capacity)snprintf(error,capacity,"%s:line=0",attribute_error);
                free(state);
                return false;
            }
        }
    }
    /* The explicit legacy Base-I audit is independent of -march. A raw-byte
     * ban must never be activated by ISA selection alone: GCC Linux input
     * legitimately uses `.4byte` instructions inside executable sections. */
    state->reject_raw_exec_fragments = state->strict_i_xlen != 0U;
    *out=state;
    return true;
}

static void riscv_destroy_target_state(void *target_state) {
    free(target_state);
}

static bool riscv_configure_legacy(MiniAsTargetSpec *config,
                                   unsigned strict_i_xlen) {
    MiniAsRiscvProfile profile;
    if (strict_i_xlen != 32U && strict_i_xlen != 64U) return false;
    if (config->object.elf32 != (strict_i_xlen == 32U)) return false;
    memset(&profile, 0, sizeof(profile));
    profile.elf32 = config->object.elf32;
    profile.strict_i_xlen = strict_i_xlen;
    if (sizeof(profile) > sizeof(config->profile)) return false;
    memcpy(config->profile, &profile, sizeof(profile));
    config->profile_size = sizeof(profile);
    return true;
}

static void riscv_capture_site_state(const void *target_state,
                                      const MiniAsSourceSiteView *stmt,
                                      MiniAsTargetSiteState *site) {
    const MiniAsRiscvState *state=target_state;
    site->mode_bits = state->options;
    if(strcmp(stmt->op,".insn")==0)
        site->provenance_bits|=MINIAS_RISCV_SITE_INSN_DATA;
}

static unsigned riscv_default_exec_alignment_for_site(
    const void *target_state) {
    const MiniAsRiscvState *state = target_state;
    if (state != NULL && state->isa_explicit &&
        (state->file_isa.features & RISCV_ISA_C) != 0U &&
        (state->options & MINIAS_RISCV_OPTION_NO_RVC) == 0U)
        return 2U;
    return 4U;
}

static void riscv_resolve_cfi_abi(MiniAsTargetABIView view, MiniAsCfiABI *abi) {
    const MiniAsRiscvState *state=view.target_state;
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
    .prepare_layout = riscv_prepare_layout,
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
    .leb128_reloc_mode = 2U, /* psABI SET_ULEB128/SUB_ULEB128 */
    .leb128_relocations_enabled=riscv_leb128_relocations_enabled,
    .allow_local_section_symbol_fixup=
        riscv_allow_local_section_symbol_fixup,
    .validate_output = strict_base_i_check_sections,
    .finalize_output = riscv_record_object_mapping_symbols,
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
    .cfi_cie_version = 3U,
    .cfi_initial_cfa_register_only = true,
    .cfi_fde_range_relocations = true,
    .cfi_advance_reloc_mode = 2U,
    .emit_all_section_symbols = true,
    .resolve_cfi_abi = riscv_resolve_cfi_abi,
};

const MiniAsTargetOps *minias_target_riscv(void) { return &riscv_ops; }
