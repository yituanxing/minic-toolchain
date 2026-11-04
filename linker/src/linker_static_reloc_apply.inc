/* Common ET_EXEC fixup boundary. Target owns instruction relocation
 * classification, pair matching and machine-byte writes. The caller has
 * already completed symbol selection, target pre-layout reservations,
 * placement, and target post-layout materialization. */
static bool static_apply_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    if (state->target->apply_static_relocations == NULL) {
        fprintf(state->diagnostics,
                "minic-ld: target-static-fixup-missing:%s\n",
                state->target->emulation);
        return false;
    }
    return state->target->apply_static_relocations(state, layout, plan);
}
