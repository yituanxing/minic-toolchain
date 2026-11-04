/* x86-64 ELF64 ET_EXEC psABI relocations. No x86 machine instruction
 * encoder is needed for the currently admitted scalar/PC-relative forms. */

static MiniLdNonRiscvFixup minild_x86_static_fixup_class(uint32_t type) {
    switch (type) {
    case R_X86_64_NONE: return MINILD_NONRISCV_NONE;
    case R_X86_64_64: return MINILD_NONRISCV_ABS64;
    case R_X86_64_32: return MINILD_NONRISCV_ABS32;
    case R_X86_64_32S: return MINILD_NONRISCV_ABS32_SIGNED;
    case R_X86_64_PC32:
    case R_X86_64_PLT32: return MINILD_NONRISCV_PCREL32;
    default: return MINILD_NONRISCV_INVALID;
    }
}

static bool minild_x86_apply_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    static const MiniLdNonRiscvStaticOps ops = {
        minild_x86_static_fixup_class, NULL, NULL
    };

    return minild_nonriscv_apply_with_ops(state, layout, plan, &ops);
}
