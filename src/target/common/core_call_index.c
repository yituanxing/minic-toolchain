#include "target/common/core_call_view.h"
#include "target/common/core_instruction_family.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* One checked, compact semantic CALL snapshot per emitted Core definition.
 * Views reference the original immutable function's signature/arguments.
 * The target may still reject a valid call by its own psABI/ISA policy. */
bool minic_core_call_index_build(const MinicCoreFunction *function,
                                 size_t expected_calls, MinicCoreCallIndex *index) {
    size_t i, count = 0U;
    if (function == NULL || index == NULL || index->sites != NULL ||
        index->count != 0U ||
        (function->instruction_count != 0U && function->instructions == NULL) ||
        expected_calls > SIZE_MAX / sizeof(*index->sites))
        return false;
    if (expected_calls != 0U) {
        index->sites = (MinicCoreCallSite *)calloc(
            expected_calls, sizeof(*index->sites));
        if (index->sites == NULL) return false;
    }
    for (i = 0U; i < function->instruction_count; ++i) {
        const MinicCoreInstruction *ins = &function->instructions[i];
        if (!minic_core_instruction_is_call(ins->kind)) continue;
        if (count >= expected_calls ||
            !minic_core_call_view_build(function, ins, &index->sites[count].view)) {
            minic_core_call_index_destroy(index);
            return false;
        }
        index->sites[count].instruction_id = i;
        ++count;
    }
    if (count != expected_calls) {
        minic_core_call_index_destroy(index);
        return false;
    }
    index->count = count;
    return true;
}

/* Binary lookup avoids a pointer-sized entry for every non-CALL opcode;
 * most Linux functions have few call sites and many non-CALL instructions. */
const MinicCoreCallView *minic_core_call_index_find(
    const MinicCoreCallIndex *index, size_t instruction_id,
    size_t *ordinal) {
    size_t low = 0U, high;
    if (ordinal == NULL || index == NULL ||
        (index->count != 0U && index->sites == NULL))
        return NULL;
    high = index->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2U;
        size_t id = index->sites[mid].instruction_id;
        if (id < instruction_id) low = mid + 1U;
        else high = mid;
    }
    if (low >= index->count ||
        index->sites[low].instruction_id != instruction_id)
        return NULL;
    *ordinal = low;
    return &index->sites[low].view;
}

void minic_core_call_index_destroy(MinicCoreCallIndex *index) {
    if (index == NULL) return;
    free(index->sites);
    (void)memset(index, 0, sizeof(*index));
}

/* Production borrows the visitor-owned verified index without scanning.
 * Standalone API callers construct precisely the same semantic index once.
 * A successful empty function still has a non-NULL index for the lifetime
 * of the handle, so all machine caches share the same lookup contract. */
bool minic_core_call_index_handle_prepare(
    const MinicCoreFunction *function, const MinicCoreCallIndex *borrowed,
    MinicCoreCallIndexHandle *handle) {
    size_t i, calls = 0U;
    if (function == NULL || handle == NULL || handle->index != NULL ||
        handle->owned.sites != NULL || handle->owned.count != 0U ||
        (function->instruction_count != 0U && function->instructions == NULL))
        return false;
    if (borrowed != NULL) {
        if (borrowed->count > function->instruction_count ||
            (borrowed->count != 0U && borrowed->sites == NULL))
            return false;
        handle->index = borrowed;
        return true;
    }
    for (i = 0U; i < function->instruction_count; ++i)
        if (minic_core_instruction_is_call(function->instructions[i].kind))
            ++calls;
    if (!minic_core_call_index_build(function, calls, &handle->owned))
        return false;
    handle->index = &handle->owned;
    return true;
}

void minic_core_call_index_handle_destroy(MinicCoreCallIndexHandle *handle) {
    if (handle == NULL) return;
    minic_core_call_index_destroy(&handle->owned);
    handle->index = NULL;
}
