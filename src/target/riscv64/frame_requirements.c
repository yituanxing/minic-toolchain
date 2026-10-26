#include "target/riscv64/frame_requirements.h"
#include "target/common/core_instruction_family.h"
#include "target/common/core_call_view.h"
#include "target/riscv64/abi.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Five-target semantic identity arrives from Common, once. RV64 computes
 * precisely one native psABI plan for each direct/indirect CALL. Caller,
 * frame outgoing size and emission all consume that same placement. */
bool minic_riscv64_call_plan_cache_build(
    const MinicC0Program *program, const MinicCoreFunction *function,
    bool lp64_soft_float, const MinicCoreCallIndex *calls,
    MinicRiscv64CallPlanCache *cache) {
    size_t ordinal, previous = SIZE_MAX, maximum_stack_slots = 0U;
    if (program == NULL || function == NULL || calls == NULL || cache == NULL ||
        cache->sites != NULL || cache->count != 0U ||
        calls->count > function->instruction_count ||
        (calls->count != 0U && calls->sites == NULL) ||
        (function->instruction_count != 0U && function->instructions == NULL) ||
        calls->count > SIZE_MAX / sizeof(*cache->sites))
        return false;
    if (calls->count == 0U) return true;
    cache->sites = calloc(calls->count, sizeof(*cache->sites));
    if (cache->sites == NULL) return false;
    cache->count = calls->count;
    for (ordinal = 0U; ordinal < calls->count; ++ordinal) {
        const MinicCoreCallSite *site = &calls->sites[ordinal];
        const MinicCoreCallView *call = &site->view;
        const size_t id = site->instruction_id;
        MinicRiscv64PreparedCall *prepared;
        MinicRiscv64AbiCursor cursor;
        size_t bytes, argument_index;
        if (id >= function->instruction_count ||
            (previous != SIZE_MAX && id <= previous) ||
            !minic_core_instruction_is_call(function->instructions[id].kind) ||
            call->argument_count > (SIZE_MAX - sizeof(*prepared)) /
                                   sizeof(*prepared->arguments))
            goto fail;
        bytes = sizeof(*prepared) +
                call->argument_count * sizeof(*prepared->arguments);
        prepared = malloc(bytes);
        if (prepared == NULL) goto fail;
        prepared->instruction_id = id;
        prepared->argument_count = call->argument_count;
        cache->sites[ordinal] = prepared;
        if (!minic_riscv64_abi_cursor_initialize_for_return_profile(
                program, call->return_type, lp64_soft_float,
                &cursor, &prepared->return_value))
            goto fail;
        for (argument_index = 0U; argument_index < call->argument_count;
             ++argument_index) {
            MinicType actual_type, placement_type;
            const bool fixed = argument_index < call->parameter_count;
            if (!minic_core_call_view_argument_type(
                    function, call, argument_index, &actual_type))
                goto fail;
            placement_type = fixed
                ? call->parameter_types[argument_index] : actual_type;
            if (!minic_riscv64_abi_place_argument(
                    program, placement_type, fixed, &cursor,
                    &prepared->arguments[argument_index]))
                goto fail;
        }
        if (cursor.stack_slot_count > maximum_stack_slots)
            maximum_stack_slots = cursor.stack_slot_count;
        previous = id;
    }
    if (maximum_stack_slots > SIZE_MAX / 8U) goto fail;
    cache->maximum_stack_bytes = maximum_stack_slots * 8U;
    return true;
fail:
    minic_riscv64_call_plan_cache_destroy(cache);
    return false;
}

void minic_riscv64_call_plan_cache_destroy(MinicRiscv64CallPlanCache *cache) {
    size_t i;
    if (cache == NULL) return;
    for (i = 0U; i < cache->count; ++i)
        free(cache->sites[i]);
    free(cache->sites);
    (void)memset(cache, 0, sizeof(*cache));
}
