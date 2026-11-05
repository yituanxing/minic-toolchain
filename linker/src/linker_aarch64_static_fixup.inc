/* AArch64 ELF64 ET_EXEC relocation policy and machine instruction fields.
 * Class-neutral S+A/P and scalar writing remain in the shared static pass. */

#include "linker_aarch64_erratum843419.inc"

/* The Linux kernel's -shared -Bsymbolic -T image is not a user-space DSO.
 * Target-owned R_AARCH64_RELATIVE records in .rela.dyn preserve allocated
 * absolute pointers without applying their image-base values. All other
 * admitted relocations are invariant under a common 4KiB-aligned rebase:
 * PC-relative S-P, page-relative PAGE(S)-PAGE(P), or the low 12 bits.
 * MOVW, TLS and other runtime-dependent relocations stay fail-closed. */
#ifndef R_AARCH64_GOTPCREL32
#define R_AARCH64_GOTPCREL32 315
#endif
static bool minild_aarch64_kernel_rebase_invariant(uint32_t type) {
    switch (type) {
    /* No value, or a full-width PC-relative scalar. */
    case R_AARCH64_NONE:
    /* Converted to R_AARCH64_RELATIVE where the target is image-relative. */
    case R_AARCH64_ABS64:
    /* Post-layout checked: only image-invariant absolute values are legal. */
    case R_AARCH64_ABS32:
    case R_AARCH64_PREL32:
    case R_AARCH64_PREL64:
    /* PC-relative and page-relative A64 instruction fields. */
    case R_AARCH64_ADR_PREL_LO21:
    case R_AARCH64_ADR_PREL_PG_HI21:
    case R_AARCH64_ADR_PREL_PG_HI21_NC:
    case R_AARCH64_LD_PREL_LO19:
    case R_AARCH64_TSTBR14:
    case R_AARCH64_CONDBR19:
    case R_AARCH64_CALL26:
    case R_AARCH64_JUMP26:
    /* Only the low 12 bits of page-aligned rebasing participate. */
    case R_AARCH64_ADD_ABS_LO12_NC:
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_LDST16_ABS_LO12_NC:
    case R_AARCH64_LDST32_ABS_LO12_NC:
    case R_AARCH64_LDST64_ABS_LO12_NC:
    case R_AARCH64_LDST128_ABS_LO12_NC:
    /* GOT address calculations are invariant under common rebasing; the
     * actual GOT pointer is separately emitted as R_AARCH64_RELATIVE. */
    case R_AARCH64_GOTREL64:
    case R_AARCH64_GOTREL32:
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_LD64_GOTOFF_LO15:
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_LD64_GOTPAGE_LO15:
    case R_AARCH64_GOTPCREL32:
        return true;
    default:
        return false;
    }
}

static MiniLdNonRiscvFixup minild_aarch64_static_fixup_class(uint32_t type) {
    switch (type) {
    case R_AARCH64_NONE: return MINILD_NONRISCV_NONE;
    case R_AARCH64_ABS64: return MINILD_NONRISCV_ABS64;
    case R_AARCH64_ABS32: return MINILD_NONRISCV_ABS32;
    case R_AARCH64_GOTREL64: return MINILD_NONRISCV_ABS64;
    case R_AARCH64_GOTREL32: return MINILD_NONRISCV_ABS32_SIGNED;
    case R_AARCH64_GOTPCREL32: return MINILD_NONRISCV_PCREL32;
    case R_AARCH64_PREL64: return MINILD_NONRISCV_PCREL64;
    case R_AARCH64_PREL32: return MINILD_NONRISCV_PCREL32;
    case R_AARCH64_MOVW_UABS_G0:
    case R_AARCH64_MOVW_UABS_G0_NC:
    case R_AARCH64_MOVW_UABS_G1:
    case R_AARCH64_MOVW_UABS_G1_NC:
    case R_AARCH64_MOVW_UABS_G2:
    case R_AARCH64_MOVW_UABS_G2_NC:
    case R_AARCH64_MOVW_UABS_G3:
    case R_AARCH64_MOVW_SABS_G0:
    case R_AARCH64_MOVW_SABS_G1:
    case R_AARCH64_MOVW_SABS_G2:
    case R_AARCH64_LD_PREL_LO19:
    case R_AARCH64_ADR_PREL_LO21:
    case R_AARCH64_ADR_PREL_PG_HI21_NC:
    case R_AARCH64_TSTBR14:
    case R_AARCH64_CONDBR19:
    case R_AARCH64_CALL26:
    case R_AARCH64_JUMP26:
    case R_AARCH64_ADR_PREL_PG_HI21:
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
    case R_AARCH64_LD64_GOTOFF_LO15:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
    case R_AARCH64_LD64_GOTPAGE_LO15:
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G1:
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC:
    case R_AARCH64_TLSLE_MOVW_TPREL_G2:
    case R_AARCH64_TLSLE_MOVW_TPREL_G1:
    case R_AARCH64_TLSLE_MOVW_TPREL_G1_NC:
    case R_AARCH64_TLSLE_MOVW_TPREL_G0:
    case R_AARCH64_TLSLE_MOVW_TPREL_G0_NC:
    case R_AARCH64_TLSLE_ADD_TPREL_HI12:
    case R_AARCH64_TLSLE_ADD_TPREL_LO12:
    case R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
    case R_AARCH64_TLSLE_LDST8_TPREL_LO12:
    case R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC:
    case R_AARCH64_TLSLE_LDST16_TPREL_LO12:
    case R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC:
    case R_AARCH64_TLSLE_LDST32_TPREL_LO12:
    case R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC:
    case R_AARCH64_TLSLE_LDST64_TPREL_LO12:
    case R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC:
    case R_AARCH64_TLSLE_LDST128_TPREL_LO12:
    case R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC:
    case R_AARCH64_ADD_ABS_LO12_NC:
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_LDST16_ABS_LO12_NC:
    case R_AARCH64_LDST32_ABS_LO12_NC:
    case R_AARCH64_LDST64_ABS_LO12_NC:
    case R_AARCH64_LDST128_ABS_LO12_NC:
        return MINILD_NONRISCV_TARGET_INSTRUCTION;
    default: return MINILD_NONRISCV_INVALID;
    }
}

static size_t minild_aarch64_discarded_scalar_width(uint32_t type) {
    switch (type) {
    case R_AARCH64_ABS64:
    case R_AARCH64_PREL64: return 8U;
    case R_AARCH64_ABS32:
    case R_AARCH64_PREL32: return 4U;
    default: return 0U;
    }
}

/* Encode the complete A64 MOVW family: checking relocations select MOVZ
 * (positive) or MOVN (negative), whereas _NC forms must retain MOVK. */
static bool minild_aarch64_static_write_tls_movw(
    unsigned char *slot, uint64_t value, unsigned group, bool checking) {
    uint32_t word = load_u32le(slot);
    uint64_t magnitude = (value >> 63U) != 0U ? ~value : value;
    uint32_t imm;

    if (group > 2U || ((word >> 21U) & 3U) != group) {
        return false;
    }
    if (checking) {
        uint64_t limit = UINT64_C(1) << ((group + 1U) * 16U);

        if ((word & UINT32_C(0x9f800000)) != UINT32_C(0x92800000) ||
            (value >= limit && value < UINT64_MAX - (limit - 1U))) {
            return false;
        }
        word &= ~UINT32_C(0x601fffe0);
        word |= ((value >> 63U) == 0U ? 2U : 0U) << 29U;
    } else {
        if ((word & UINT32_C(0xff800000)) != UINT32_C(0xf2800000)) {
            return false;
        }
        word &= ~UINT32_C(0x001fffe0);
        magnitude = value;
    }
    imm = ((uint32_t)(magnitude >> (group * 16U))) & UINT32_C(0xffff);
    word |= imm << 5U;
    store_u32le(slot, word);
    return true;
}

/* AAELF64 MOVW absolute relocations are a family, not the TLS-specialized
 * MOVW subset.  _NC means MOVK with truncated bits, whereas checked unsigned
 * members require all higher chunks to be zero.  Signed members select MOVN
 * for negative absolute values and validate the full magnitude. */
static bool minild_aarch64_static_write_absolute_movw(
    unsigned char *slot, uint64_t value, unsigned group, bool nonchecking,
    bool signed_value) {
    uint32_t word = load_u32le(slot);
    uint32_t opcode = word & UINT32_C(0x7f800000);
    uint64_t magnitude = value;
    uint64_t limit;

    if (group > 3U || ((word >> 21U) & 3U) != group ||
        (group > 1U && (word & UINT32_C(0x80000000)) == 0U)) {
        return false;
    }
    if (opcode != UINT32_C(0x12800000) &&
        opcode != UINT32_C(0x52800000) &&
        opcode != UINT32_C(0x72800000)) {
        return false;
    }
    if (nonchecking) {
        /* GNU accepts both MOVZ and MOVK spelling with an _NC modifier;
         * unlike a checked relocation, the source opcode remains intact. */
        if (opcode == UINT32_C(0x12800000)) magnitude = ~value;
    } else {
        if (signed_value && opcode == UINT32_C(0x72800000)) {
            return false;
        }
        if (group < 3U) {
            limit = UINT64_C(1) << ((group + 1U) * 16U);
            if (signed_value) {
                /* Complement is the A64 MOVN immediate, including -1. */
                if (value >= limit && (~value) >= limit) {
                    return false;
                }
            } else if (value >= limit) {
                return false;
            }
        }
        if (signed_value && (value >> 63U) != 0U) {
            magnitude = ~value;
            word &= ~UINT32_C(0x60000000);
        } else if (opcode != UINT32_C(0x72800000)) {
            word &= ~UINT32_C(0x60000000);
            word |= UINT32_C(0x40000000);
        }
    }
    word &= ~UINT32_C(0x001fffe0);
    word |= ((uint32_t)(magnitude >> (group * 16U)) & UINT32_C(0xffff)) << 5U;
    store_u32le(slot, word);
    return true;
}

static bool minild_aarch64_static_write_instruction(
    const MiniLdState *state, unsigned char *slot, const MiniLdReloc *reloc,
    uint64_t target, uint64_t place, bool undefined_weak) {
    uint64_t delta = target - place;
    uint32_t instruction;
    uint32_t imm;

    (void)state;
    (void)undefined_weak;

    switch (reloc->type) {
    case R_AARCH64_MOVW_UABS_G0:
    case R_AARCH64_MOVW_UABS_G0_NC:
    case R_AARCH64_MOVW_UABS_G1:
    case R_AARCH64_MOVW_UABS_G1_NC:
    case R_AARCH64_MOVW_UABS_G2:
    case R_AARCH64_MOVW_UABS_G2_NC:
    case R_AARCH64_MOVW_UABS_G3:
    case R_AARCH64_MOVW_SABS_G0:
    case R_AARCH64_MOVW_SABS_G1:
    case R_AARCH64_MOVW_SABS_G2: {
        bool signed_value = reloc->type >= R_AARCH64_MOVW_SABS_G0;
        bool nonchecking = !signed_value &&
                           ((reloc->type - R_AARCH64_MOVW_UABS_G0) & 1U) != 0U;
        unsigned group = signed_value
                             ? reloc->type - R_AARCH64_MOVW_SABS_G0
                             : (reloc->type - R_AARCH64_MOVW_UABS_G0) / 2U;

        return minild_aarch64_static_write_absolute_movw(
            slot, target, group, nonchecking, signed_value);
    }
    case R_AARCH64_ADR_PREL_LO21: {
        if (!minild_nonriscv_signed_fits(delta, 21U)) {
            return false;
        }
        instruction = load_u32le(slot);
        if ((instruction & UINT32_C(0x9f000000)) != UINT32_C(0x10000000)) {
            return false;
        }
        imm = (uint32_t)delta & UINT32_C(0x1fffff);
        instruction &= ~UINT32_C(0x60ffffe0);
        instruction |= (imm & 3U) << 29U;
        instruction |= ((imm >> 2U) & UINT32_C(0x7ffff)) << 5U;
        store_u32le(slot, instruction);
        return true;
    }
    case R_AARCH64_LD_PREL_LO19:
    case R_AARCH64_CONDBR19:
    case R_AARCH64_TSTBR14: {
        unsigned shift = reloc->type == R_AARCH64_TSTBR14 ? 14U : 19U;

        if ((delta & 3U) != 0U ||
            !minild_nonriscv_signed_fits(delta, shift + 2U)) {
            return false;
        }
        instruction = load_u32le(slot);
        if (reloc->type == R_AARCH64_LD_PREL_LO19) {
            if ((instruction & UINT32_C(0x3b000000)) !=
                UINT32_C(0x18000000)) {
                return false;
            }
        } else if (reloc->type == R_AARCH64_CONDBR19) {
            if ((instruction & UINT32_C(0xff000010)) !=
                    UINT32_C(0x54000000) &&
                (instruction & UINT32_C(0x7e000000)) !=
                    UINT32_C(0x34000000)) {
                return false;
            }
        } else if ((instruction & UINT32_C(0x7e000000)) !=
                   UINT32_C(0x36000000)) {
            return false;
        }
        instruction &= ~(shift == 14U ? UINT32_C(0x0007ffe0) :
                        UINT32_C(0x00ffffe0));
        instruction |= (((uint32_t)delta >> 2U) &
                        ((UINT32_C(1) << shift) - 1U)) << 5U;
        store_u32le(slot, instruction);
        return true;
    }
    case R_AARCH64_CALL26:
    case R_AARCH64_JUMP26:
        if ((delta & UINT64_C(3)) != 0U ||
            !minild_nonriscv_signed_fits(delta, 28U)) {
            return false;
        }
        instruction = load_u32le(slot);
        if ((reloc->type == R_AARCH64_CALL26 &&
             (instruction & UINT32_C(0xfc000000)) != UINT32_C(0x94000000)) ||
            (reloc->type == R_AARCH64_JUMP26 &&
             (instruction & UINT32_C(0xfc000000)) != UINT32_C(0x14000000))) {
            return false;
        }
        store_u32le(slot, (instruction & UINT32_C(0xfc000000)) |
                           (((uint32_t)delta >> 2U) & UINT32_C(0x03ffffff)));
        return true;
    case R_AARCH64_ADR_PREL_PG_HI21:
    case R_AARCH64_ADR_PREL_PG_HI21_NC:
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21: {
        uint64_t page_delta = (target >> 12U) - (place >> 12U);

        if (reloc->type != R_AARCH64_ADR_PREL_PG_HI21_NC &&
            !minild_nonriscv_signed_fits(page_delta, 21U)) {
            return false;
        }
        instruction = load_u32le(slot);
        if ((instruction & UINT32_C(0x9f000000)) != UINT32_C(0x90000000)) {
            return false;
        }
        imm = (uint32_t)page_delta & UINT32_C(0x1fffff);
        instruction &= ~UINT32_C(0x60ffffe0);
        instruction |= (imm & 3U) << 29U;
        instruction |= ((imm >> 2U) & UINT32_C(0x7ffff)) << 5U;
        store_u32le(slot, instruction);
        return true;
    }
    case R_AARCH64_ADD_ABS_LO12_NC:
    case R_AARCH64_TLSLE_ADD_TPREL_HI12:
    case R_AARCH64_TLSLE_ADD_TPREL_LO12:
    case R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
        instruction = load_u32le(slot);
        if ((instruction & UINT32_C(0x7f400000)) !=
            (reloc->type == R_AARCH64_TLSLE_ADD_TPREL_HI12 ?
             UINT32_C(0x11400000) : UINT32_C(0x11000000)) ||
            (reloc->type == R_AARCH64_TLSLE_ADD_TPREL_HI12 &&
             target >= (UINT64_C(1) << 24U)) ||
            (reloc->type == R_AARCH64_TLSLE_ADD_TPREL_LO12 &&
             target >= (UINT64_C(1) << 12U))) {
            return false;
        }
        instruction &= ~UINT32_C(0x003ffc00);
        instruction |= (((uint32_t)(target >>
                         (reloc->type == R_AARCH64_TLSLE_ADD_TPREL_HI12 ?
                          12U : 0U))) & UINT32_C(0xfff)) << 10U;
        store_u32le(slot, instruction);
        return true;
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_LDST16_ABS_LO12_NC:
    case R_AARCH64_LDST32_ABS_LO12_NC:
    case R_AARCH64_LDST64_ABS_LO12_NC:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
    case R_AARCH64_LDST128_ABS_LO12_NC: {
        unsigned shift = reloc->type == R_AARCH64_LDST8_ABS_LO12_NC ? 0U :
                         reloc->type == R_AARCH64_LDST16_ABS_LO12_NC ? 1U :
                         reloc->type == R_AARCH64_LDST32_ABS_LO12_NC ? 2U :
                         (reloc->type == R_AARCH64_LDST64_ABS_LO12_NC ||
                          reloc->type == R_AARCH64_LD64_GOT_LO12_NC ||
                          reloc->type == R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC) ? 3U : 4U;
        uint32_t mask = (UINT32_C(1) << shift) - 1U;

        /* Scaled unsigned load/store immediates must retain alignment. */
        if (((uint32_t)target & mask) != 0U) {
            return false;
        }
        instruction = load_u32le(slot);
        if ((reloc->type == R_AARCH64_LD64_GOT_LO12_NC ||
             reloc->type == R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC) &&
            (instruction & UINT32_C(0xffc00000)) != UINT32_C(0xf9400000)) {
            return false;
        }
        instruction &= ~UINT32_C(0x003ffc00);
        instruction |= ((((uint32_t)target & UINT32_C(0xfff)) >> shift) &
                        UINT32_C(0xfff)) << 10U;
        store_u32le(slot, instruction);
        return true;
    }
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
        /* LDR (literal, 64-bit): 19 signed words, PC-relative. */
        if ((delta & 3U) != 0U ||
            !minild_nonriscv_signed_fits(delta, 21U)) {
            return false;
        }
        instruction = load_u32le(slot);
        if ((instruction & UINT32_C(0xff000000)) != UINT32_C(0x58000000)) {
            return false;
        }
        instruction &= ~UINT32_C(0x00ffffe0);
        instruction |= (((uint32_t)delta >> 2U) & UINT32_C(0x7ffff)) << 5U;
        store_u32le(slot, instruction);
        return true;
    case R_AARCH64_LD64_GOTOFF_LO15:
    case R_AARCH64_LD64_GOTPAGE_LO15:
        /* LD64 GOT offsets are 15-bit bytes scaled by 8, not LO12. */
        if ((target & 7U) != 0U || target >= UINT64_C(0x8000)) {
            return false;
        }
        instruction = load_u32le(slot);
        if ((instruction & UINT32_C(0xffc00000)) != UINT32_C(0xf9400000)) {
            return false;
        }
        instruction &= ~UINT32_C(0x003ffc00);
        instruction |= ((uint32_t)(target >> 3U) & UINT32_C(0xfff)) << 10U;
        store_u32le(slot, instruction);
        return true;
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G1:
    case R_AARCH64_TLSLE_MOVW_TPREL_G1:
        return minild_aarch64_static_write_tls_movw(slot, target, 1U, true);
    case R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC:
    case R_AARCH64_TLSLE_MOVW_TPREL_G0_NC:
        return minild_aarch64_static_write_tls_movw(slot, target, 0U, false);
    case R_AARCH64_TLSLE_MOVW_TPREL_G2:
        return minild_aarch64_static_write_tls_movw(slot, target, 2U, true);
    case R_AARCH64_TLSLE_MOVW_TPREL_G1_NC:
        return minild_aarch64_static_write_tls_movw(slot, target, 1U, false);
    case R_AARCH64_TLSLE_MOVW_TPREL_G0:
        return minild_aarch64_static_write_tls_movw(slot, target, 0U, true);
    case R_AARCH64_TLSLE_LDST8_TPREL_LO12:
    case R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC:
    case R_AARCH64_TLSLE_LDST16_TPREL_LO12:
    case R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC:
    case R_AARCH64_TLSLE_LDST32_TPREL_LO12:
    case R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC:
    case R_AARCH64_TLSLE_LDST64_TPREL_LO12:
    case R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC:
    case R_AARCH64_TLSLE_LDST128_TPREL_LO12:
    case R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC: {
        unsigned shift = reloc->type >= R_AARCH64_TLSLE_LDST128_TPREL_LO12 ?
                             4U :
                         reloc->type >= R_AARCH64_TLSLE_LDST64_TPREL_LO12 ?
                             3U :
                         reloc->type >= R_AARCH64_TLSLE_LDST32_TPREL_LO12 ?
                             2U :
                         reloc->type >= R_AARCH64_TLSLE_LDST16_TPREL_LO12 ?
                             1U : 0U;
        bool checked = reloc->type == R_AARCH64_TLSLE_LDST8_TPREL_LO12 ||
                       reloc->type == R_AARCH64_TLSLE_LDST16_TPREL_LO12 ||
                       reloc->type == R_AARCH64_TLSLE_LDST32_TPREL_LO12 ||
                       reloc->type == R_AARCH64_TLSLE_LDST64_TPREL_LO12 ||
                       reloc->type == R_AARCH64_TLSLE_LDST128_TPREL_LO12;

        instruction = load_u32le(slot);
        if ((target & ((UINT64_C(1) << shift) - 1U)) != 0U ||
            (checked && target >= UINT64_C(0x1000)) ||
            (instruction & UINT32_C(0x3b000000)) != UINT32_C(0x39000000) ||
            (shift == 4U ?
              (instruction & UINT32_C(0x3f000000)) != UINT32_C(0x3d000000) :
              (((instruction >> 30U) & 3U) != shift ||
               (instruction & UINT32_C(0x04000000)) != 0U))) {
            return false;
        }
        instruction &= ~UINT32_C(0x003ffc00);
        instruction |= (((uint32_t)target & UINT32_C(0xfff)) >> shift) << 10U;
        store_u32le(slot, instruction);
        return true;
    }
    default:
        return false;
    }
}

static bool minild_aarch64_apply_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    const MiniLdStaticTargetPlan *plan) {
    static const MiniLdNonRiscvStaticOps ops = {
        minild_aarch64_static_fixup_class,
        minild_aarch64_static_resolve_target,
        minild_nonriscv_word_instruction_width,
        minild_aarch64_static_write_instruction,
        minild_aarch64_kernel_relative_target,
        minild_aarch64_discarded_scalar_width
    };

    return minild_nonriscv_apply_with_ops(state, layout, output_plan,
                                         plan, &ops) &&
           minild_aarch64_843419_emit_branches(state, layout, plan);
}
