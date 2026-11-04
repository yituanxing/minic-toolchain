/* Common ET_EXEC lifetime: the target owns any relocation-specific
 * reservation, without exposing RISC-V GOT slots to the linker pipeline. */
typedef struct MiniLdStaticTargetPlan {
    void *target_data;
} MiniLdStaticTargetPlan;

static void static_target_plan_destroy(const MiniLdState *state,
                                        MiniLdStaticTargetPlan *plan) {
    if (state->target != NULL &&
        state->target->destroy_static_relocations != NULL) {
        state->target->destroy_static_relocations(plan);
    }
    plan->target_data = NULL;
}

#include "linker_static_reloc_scan.inc"

static bool static_fill_target_relocations(
    MiniLdState *state, const MiniLdLayout *layout,
    const MiniLdStaticTargetPlan *plan) {
    return state->target->fill_static_relocations == NULL ||
           state->target->fill_static_relocations(state, layout, plan);
}
