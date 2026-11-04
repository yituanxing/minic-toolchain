/* AArch64 ELF64 ET_EXEC relocation policy and machine instruction fields.
 * Class-neutral S+A/P and scalar writing remain in the shared static pass. */

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
    case R_AARCH64_CALL26:
    case R_AARCH64_JUMP26:
    case R_AARCH64_ADR_PREL_PG_HI21:
    case R_AARCH64_ADR_GOT_PAGE:
    case R_AARCH64_GOT_LD_PREL19:
    case R_AARCH64_LD64_GOTOFF_LO15:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_LD64_GOTPAGE_LO15:
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

static bool minild_aarch64_static_write_instruction(
    unsigned char *slot, const MiniLdReloc *reloc,
    uint64_t target, uint64_t place, bool undefined_weak) {
    uint64_t delta = target - place;
    uint32_t instruction;
    uint32_t imm;

    (void)undefined_weak;

    switch (reloc->type) {
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
    case R_AARCH64_ADR_GOT_PAGE: {
        uint64_t page_delta = (target >> 12U) - (place >> 12U);

        if (!minild_nonriscv_signed_fits(page_delta, 21U)) {
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
        instruction = load_u32le(slot);
        if ((instruction & UINT32_C(0x7f400000)) != UINT32_C(0x11000000)) {
            return false;
        }
        instruction &= ~UINT32_C(0x003ffc00);
        instruction |= ((uint32_t)target & UINT32_C(0xfff)) << 10U;
        store_u32le(slot, instruction);
        return true;
    case R_AARCH64_LDST8_ABS_LO12_NC:
    case R_AARCH64_LDST16_ABS_LO12_NC:
    case R_AARCH64_LDST32_ABS_LO12_NC:
    case R_AARCH64_LDST64_ABS_LO12_NC:
    case R_AARCH64_LD64_GOT_LO12_NC:
    case R_AARCH64_LDST128_ABS_LO12_NC: {
        unsigned shift = reloc->type == R_AARCH64_LDST8_ABS_LO12_NC ? 0U :
                         reloc->type == R_AARCH64_LDST16_ABS_LO12_NC ? 1U :
                         reloc->type == R_AARCH64_LDST32_ABS_LO12_NC ? 2U :
                         (reloc->type == R_AARCH64_LDST64_ABS_LO12_NC ||
                          reloc->type == R_AARCH64_LD64_GOT_LO12_NC) ? 3U : 4U;
        uint32_t mask = (UINT32_C(1) << shift) - 1U;

        /* Scaled unsigned load/store immediates must retain alignment. */
        if (((uint32_t)target & mask) != 0U) {
            return false;
        }
        instruction = load_u32le(slot);
        if (reloc->type == R_AARCH64_LD64_GOT_LO12_NC &&
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
    default:
        return false;
    }
}

static bool minild_aarch64_apply_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    static const MiniLdNonRiscvStaticOps ops = {
        minild_aarch64_static_fixup_class,
        minild_aarch64_static_resolve_target,
        minild_nonriscv_word_instruction_width,
        minild_aarch64_static_write_instruction
    };

    return minild_nonriscv_apply_with_ops(state, layout, plan, &ops);
}
