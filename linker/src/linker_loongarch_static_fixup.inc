/* LoongArch64 ELF64 ET_EXEC psABI classification and machine fields.
 * PCALA's rounded high part is specific to the LoongArch psABI. */

static MiniLdNonRiscvFixup minild_loongarch_static_fixup_class(uint32_t type) {
    switch (type) {
    case R_LARCH_NONE:
    case R_LARCH_MARK_LA:
    case R_LARCH_MARK_PCREL:
    case R_LARCH_RELAX: return MINILD_NONRISCV_NONE;
    case R_LARCH_64: return MINILD_NONRISCV_ABS64;
    case R_LARCH_32: return MINILD_NONRISCV_ABS32;
    case R_LARCH_64_PCREL: return MINILD_NONRISCV_PCREL64;
    case R_LARCH_32_PCREL: return MINILD_NONRISCV_PCREL32;
    case R_LARCH_B26:
    case R_LARCH_ABS_HI20:
    case R_LARCH_ABS_LO12:
    case R_LARCH_PCALA_HI20:
    case R_LARCH_PCALA_LO12:
        return MINILD_NONRISCV_TARGET_INSTRUCTION;
    default: return MINILD_NONRISCV_INVALID;
    }
}

static bool minild_loongarch_static_write_instruction(
    unsigned char *slot, const MiniLdReloc *reloc,
    uint64_t target, uint64_t place) {
    uint64_t delta = target - place;
    uint64_t page_delta;
    uint32_t instruction;
    uint32_t imm;

    switch (reloc->type) {
    case R_LARCH_B26:
        if ((delta & UINT64_C(3)) != 0U ||
            !minild_nonriscv_signed_fits(delta, 28U)) {
            return false;
        }
        instruction = load_u32le(slot) & ~UINT32_C(0x03ffffff);
        instruction |= ((uint32_t)delta >> 18U) & UINT32_C(0x3ff);
        instruction |= (((uint32_t)delta >> 2U) & UINT32_C(0xffff)) << 10U;
        store_u32le(slot, instruction);
        return true;
    case R_LARCH_ABS_HI20:
    case R_LARCH_PCALA_HI20:
        page_delta = reloc->type == R_LARCH_PCALA_HI20
                         ? ((target + UINT64_C(0x800)) >> 12U) -
                               (place >> 12U)
                         : target >> 12U;
        if ((reloc->type == R_LARCH_PCALA_HI20 &&
             !minild_nonriscv_signed_fits(page_delta, 20U)) ||
            (reloc->type == R_LARCH_ABS_HI20 && target > UINT32_MAX &&
             !minild_nonriscv_signed_fits(target, 32U))) {
            return false;
        }
        imm = (uint32_t)page_delta & UINT32_C(0xfffff);
        instruction = load_u32le(slot);
        instruction &= ~UINT32_C(0x01ffffe0);
        instruction |= imm << 5U;
        store_u32le(slot, instruction);
        return true;
    case R_LARCH_ABS_LO12:
    case R_LARCH_PCALA_LO12:
        instruction = load_u32le(slot);
        instruction &= ~UINT32_C(0x003ffc00);
        instruction |= ((uint32_t)target & UINT32_C(0xfff)) << 10U;
        store_u32le(slot, instruction);
        return true;
    default:
        return false;
    }
}

static bool minild_loongarch_apply_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    static const MiniLdNonRiscvStaticOps ops = {
        minild_loongarch_static_fixup_class,
        minild_nonriscv_word_instruction_width,
        minild_loongarch_static_write_instruction
    };

    return minild_nonriscv_apply_with_ops(state, layout, plan, &ops);
}
