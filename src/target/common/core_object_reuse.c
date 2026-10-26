#include "target/common/core_object_reuse.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Frame-owned interval graph coloring: no process-global cache or flags. */
static bool core_scalar_object_intervals_overlap(
    const CoreScalarObjectInterval *left, const CoreScalarObjectInterval *right,
    bool crossblock_enabled) {
    if (left == NULL || right == NULL || !left->reusable || !right->reusable) {
        return false;
    }
    if (left->block_index != right->block_index) {
        return !(crossblock_enabled &&
                 left->can_share_across_blocks &&
                 right->can_share_across_blocks);
    }
    return !(left->last_position < right->first_position ||
             right->last_position < left->first_position);
}

bool minic_core_object_reuse_plan_build(
    const MinicC0Program *program, const MinicCoreFunction *function,
    const MinicDataLayout *layout, size_t unit_bytes,
    bool crossblock_enabled, MinicCoreObjectReusePlan *plan) {
    size_t object_index;
    size_t peak = 0U;
    if (program == NULL || function == NULL || layout == NULL ||
        (unit_bytes != 4U && unit_bytes != 8U) || plan == NULL ||
        plan->prepared || plan->intervals != NULL || plan->slots != NULL)
        return false;
    plan->crossblock_enabled = crossblock_enabled;
    plan->object_count = function->object_count;

        if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-object-reuse-cache state=begin "
                          "objects=%zu instructions=%zu values=%zu\n",
                          function->object_count,
                          function->instruction_count,
                          function->value_count);
            (void)fflush(stderr);
        }

        if (function->object_count != 0U) {
            if (function->object_count > SIZE_MAX / sizeof(*plan->intervals) ||
                function->object_count > SIZE_MAX / sizeof(*plan->slots)) {
                minic_core_object_reuse_plan_destroy(plan);
                return false;
            }
            plan->intervals =
                (CoreScalarObjectInterval *)malloc(
                    function->object_count * sizeof(*plan->intervals));
            plan->slots =
                (size_t *)malloc(function->object_count *
                                 sizeof(*plan->slots));
            if (plan->intervals == NULL ||
                plan->slots == NULL) {
                minic_core_object_reuse_plan_destroy(plan);
                return false;
            }

            /* One checked pass at every function size; failed optional
             * indexing means private storage, never duplicate scans. */
            minic_core_object_intervals_index(function, &plan->analysis);
            for (object_index = 0U; object_index < function->object_count; ++object_index) {
                plan->slots[object_index] = SIZE_MAX;
                if (object_index > UINT32_MAX ||
                    !minic_core_object_interval(
                        program,
                        function,
                        (MinicCoreObjectId)object_index,
                        layout, unit_bytes,
                        &plan->analysis, &plan->intervals[object_index])) {
                    minic_core_object_reuse_plan_destroy(plan);
                    return false;
                }
            }

            for (object_index = 0U; object_index < function->object_count; ++object_index) {
                size_t slot;
                if (!plan->intervals[object_index].reusable) {
                    continue;
                }
                for (slot = 0U;; ++slot) {
                    size_t prior;
                    bool occupied = false;
                    for (prior = 0U; prior < object_index; ++prior) {
                        if (plan->slots[prior] == slot &&
                            core_scalar_object_intervals_overlap(
                                &plan->intervals[object_index],
                                &plan->intervals[prior], plan->crossblock_enabled)) {
                            occupied = true;
                            break;
                        }
                    }
                    if (!occupied) {
                        plan->slots[object_index] = slot;
                        if (slot == SIZE_MAX) {
                            minic_core_object_reuse_plan_destroy(plan);
                            return false;
                        }
                        if (slot + 1U > peak) {
                            peak = slot + 1U;
                        }
                        break;
                    }
                }
            }
        }

        plan->peak_slots = peak;
        if (getenv("MINIC_BOOTSTRAP_TRACE") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-object-reuse-cache state=end "
                          "objects=%zu peak_slots=%zu\n",
                          function->object_count,
                          peak);
            (void)fflush(stderr);
        }
    plan->prepared = true;
    return true;
}
bool minic_core_object_reuse_plan_get(
    const MinicCoreObjectReusePlan *plan, MinicCoreObjectId target,
    bool *reusable, size_t *slot, size_t *peak_slots) {
    if (plan == NULL || !plan->prepared || peak_slots == NULL ||
        (target != MINIC_CORE_OBJECT_INVALID && target >= plan->object_count))
        return false;
    *peak_slots = plan->peak_slots;
    if (reusable != NULL)
        *reusable = target != MINIC_CORE_OBJECT_INVALID &&
            plan->intervals != NULL && plan->intervals[target].reusable;
    if (slot != NULL)
        *slot = target != MINIC_CORE_OBJECT_INVALID && plan->slots != NULL
            ? plan->slots[target] : SIZE_MAX;
    return true;
}
void minic_core_object_reuse_plan_destroy(
    MinicCoreObjectReusePlan *plan) {
    if (plan == NULL) return;
    minic_core_object_intervals_destroy(&plan->analysis);
    free(plan->intervals);
    free(plan->slots);
    (void)memset(plan, 0, sizeof(*plan));
}
