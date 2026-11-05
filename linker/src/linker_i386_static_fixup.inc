/* ELF32 i386 ET_EXEC: SHT_REL input addends have already been normalized by
 * Target admission. Common applies the same scalar S+A and S+A-P operations
 * as every other non-RISC-V target, with i386's 8/16/32-bit field widths. */
static MiniLdNonRiscvFixup minild_i386_static_fixup_class(uint32_t type) {
    switch (type) {
    case R_386_NONE: return MINILD_NONRISCV_NONE;
    case R_386_8: return MINILD_NONRISCV_ABS8;
    case R_386_16: return MINILD_NONRISCV_ABS16;
    case R_386_32: return MINILD_NONRISCV_ABS32;
    case R_386_PC8: return MINILD_NONRISCV_PCREL8;
    case R_386_PC16: return MINILD_NONRISCV_PCREL16;
    case R_386_PC32: return MINILD_NONRISCV_PCREL32;
    default: return MINILD_NONRISCV_INVALID;
    }
}

static bool minild_i386_apply_static_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    static const MiniLdNonRiscvStaticOps ops = {
        minild_i386_static_fixup_class,
        NULL, NULL, NULL, NULL
    };

    return minild_nonriscv_apply_with_ops(state, layout, plan, &ops);
}
