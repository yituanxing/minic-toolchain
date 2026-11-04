/* Common ET_EXEC pre-layout boundary. Every architecture sees the same
 * canonical relocation workspace. Only its target may reserve GOT/other
 * synthetic data before addresses are assigned. */
static bool static_reserve_target_relocations(
    MiniLdState *state, MiniLdStaticTargetPlan *plan) {
    return state->target->reserve_static_relocations == NULL ||
           state->target->reserve_static_relocations(state, plan);
}
