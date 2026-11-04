/* RV64 shared psABI: the current eager-PLT layout and relocation operators.
 * New targets should add their own leaf, not redefine the common interface. */

static bool riscv_shared_patch_final_layout_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    const struct MiniLdSharedImage *shared);
static bool riscv_shared_finalize_plt_and_calls(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdOutputPlan *output_plan,
    const struct MiniLdSharedImage *shared);

static const MiniLdSharedTargetAbi minild_riscv64_shared_abi = {
    8U, 16U, 16U, 3U, 8U,
    24U, 24U, 16U, /* ELF64 sym, RELA and dynamic record bytes. */
    minild_target_reloc_class,
    minild_target_reloc_at_final_layout,
    minild_target_dynamic_reloc_info,
    minild_target_addend_bits,
    minild_target_absolute_width,
    minild_target_store_absolute,
    riscv_shared_patch_final_layout_relocations,
    riscv_shared_finalize_plt_and_calls
};

static const struct MiniLdSharedTargetAbi *
minild_riscv_shared_target_abi(void) {
    return &minild_riscv64_shared_abi;
}
