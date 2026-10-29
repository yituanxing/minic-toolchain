#include "target/riscv64/frame_layout.h"
#include "target/common/core_function_facts.h"
#include "target/common/core_frame_layout.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/frame_requirements.h"
#include "target/common/core_spill_layout.h"
#include "target/riscv64/record_snapshot.h"
#include "target/data_layout.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One RV64 owner of outgoing ABI, object reuse, spills and CFG planning. */
bool minic_riscv64_core_frame_initialize_with_storage(
    const MinicC0Program *program, const MinicCoreFunction *function,
    MinicRiscv64CoreFrame *frame, MinicRiscv64FunctionAbiPlan *abi_plan,
    bool crossblock_enabled, bool force_save_return_address,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage) {
    size_t object_index;
    size_t storage_size;
    size_t required_size;
    size_t outgoing_argument_size;
    size_t maximum_object_alignment;
    size_t scalar_reuse_pool_slots;

    if (function == NULL || frame == NULL || facts == NULL) {
        return false;
    }
    frame->object_offsets = NULL;
    frame->value_offsets = NULL;
    frame->reachable_blocks = NULL;
    frame->parameter_locations = NULL;
    frame->parameter_location_count = 0U;
    frame->has_named_parameter_cursor = false;
    frame->value_slot_count = 0U;
    frame->object_storage = (MinicCoreObjectStoragePlan){0};
    frame->borrows_object_storage = false;
    frame->object_reuse = (MinicCoreObjectReusePlan){0};
    frame->semantic_calls = NULL;
    frame->call_index = (MinicCoreCallIndexHandle){0};
    frame->outgoing_calls = (MinicRiscv64CallPlanCache){0};
    if (!minic_core_object_storage_plan_acquire(
            program, minic_default_data_layout(), function, storage,
            &frame->object_storage, &frame->borrows_object_storage))
        return false;
    frame->has_dynamic_stack_alloc = facts->uses_dynamic_stack_alloc;
    frame->saved_frame_base_offset = 0U;
    /* All five targets now use the same Common CALL-index ownership
     * contract. RV64's standalone API owns its generated snapshot, while
     * production borrows the visitor's exact immutable CALL index. Keep
     * the historical facts-count consistency gate for standalone callers. */
    if (facts->direct_call_count > SIZE_MAX - facts->indirect_call_count ||
        !minic_core_call_index_handle_prepare(
            function, facts->call_index, &frame->call_index))
        return false;
    frame->semantic_calls = frame->call_index.index;
    if (facts->call_index == NULL &&
        frame->semantic_calls->count !=
            facts->direct_call_count + facts->indirect_call_count)
        return false;
    if (abi_plan != NULL) {
        /* Capability admission already checked and qualified this exact
         * ordered CALL index. Transfer its psABI locations into the frame;
         * never classify the same calls again with a different FP mode. */
        if (!abi_plan->prepared ||
            abi_plan->outgoing_calls.count != frame->semantic_calls->count ||
            (frame->semantic_calls->count != 0U &&
             abi_plan->outgoing_calls.sites == NULL))
            return false;
        frame->outgoing_calls = abi_plan->outgoing_calls;
        abi_plan->outgoing_calls = (MinicRiscv64CallPlanCache){0};
    } else if (!minic_riscv64_call_plan_cache_build(
                   program, function, false,
                   frame->semantic_calls, &frame->outgoing_calls)) {
        /* Standalone legacy frame entry retains its checked fallback. */
        return false;
    }
    outgoing_argument_size = frame->outgoing_calls.maximum_stack_bytes;
    frame->outgoing_argument_size = outgoing_argument_size;
    frame->dynamic_call_area_size = 0U;
    if (frame->has_dynamic_stack_alloc &&
        !minic_core_frame_align_up(outgoing_argument_size, 16U, &frame->dynamic_call_area_size)) {
        return false;
    }
    storage_size = outgoing_argument_size;
    maximum_object_alignment = 16U;
    if (!minic_core_object_reuse_plan_build(
            program, function, minic_default_data_layout(), 8U,
            crossblock_enabled, &frame->object_reuse) ||
        !minic_core_object_reuse_plan_get(
            &frame->object_reuse, MINIC_CORE_OBJECT_INVALID,
            NULL, NULL, &scalar_reuse_pool_slots)) {
        return false;
    }
    for (object_index = 0U; object_index < function->object_count; ++object_index) {
        const MinicCoreObjectStorage *object =
            &frame->object_storage.objects[object_index];
        const size_t object_size = object->extent;
        const size_t object_alignment = object->alignment;
        if (object_size == 0U &&
            !minic_type_is_record(function->objects[object_index].type))
            return false;
        if (object_alignment > maximum_object_alignment) {
            maximum_object_alignment = object_alignment;
        }
        if (object_index <= UINT32_MAX) {
            bool scalar_reusable;
            size_t scalar_slot;
            size_t scalar_peak;
            if (!minic_core_object_reuse_plan_get(
                                                 &frame->object_reuse,
                                                 (MinicCoreObjectId)object_index,
                                                 &scalar_reusable,
                                                 &scalar_slot,
                                                 &scalar_peak)) {
                return false;
            }
            if (scalar_peak != scalar_reuse_pool_slots) {
                return false;
            }
            if (scalar_reusable) {
                if (scalar_slot >= scalar_reuse_pool_slots) {
                    return false;
                }
                continue;
            }
        }
        {
            MinicCoreObjectId reuse_candidate;
            if (object_index <= UINT32_MAX &&
                minic_riscv64_record_call_result_snapshot_reuse_candidate(
                    function, (MinicCoreObjectId)object_index, &reuse_candidate)) {
                continue;
            }
        }
        if (!minic_core_frame_align_up(storage_size, object_alignment, &storage_size) ||
            storage_size > SIZE_MAX - object_size) {
            return false;
        }
        storage_size += object_size;
    }
    if (!minic_core_frame_align_up(storage_size, 8U, &storage_size) ||
        scalar_reuse_pool_slots > (SIZE_MAX - storage_size) / 8U) {
        return false;
    }
    storage_size += scalar_reuse_pool_slots * 8U;
    if (!minic_core_frame_align_up(storage_size, 8U, &frame->value_base_offset)) {
        return false;
    }
    /* Plan all RV64 SSA offsets NOW, in the same single walk that determines
     * frame size. The old late second traversal was identical wasted work.
     * Target-specific psABI/spill base geometry remains unchanged. */
    {
        size_t maximum_block_bytes = 0U;
        size_t i;
        if (function->value_count > SIZE_MAX / sizeof(*frame->value_offsets))
            return false;
        if (function->value_count != 0U) {
            frame->value_offsets = malloc(function->value_count *
                                          sizeof(*frame->value_offsets));
            if (frame->value_offsets == NULL) return false;
            for (i = 0U; i < function->value_count; ++i)
                frame->value_offsets[i] = SIZE_MAX;
        }
        if (!minic_core_function_reused_spill_layout(
                program, function, minic_default_data_layout(), 8U,
                frame->value_base_offset, frame->value_offsets,
                &maximum_block_bytes, NULL))
            return false;
        frame->value_slot_count = maximum_block_bytes / 8U;
        storage_size = frame->value_base_offset + maximum_block_bytes;
    }
    frame->saves_return_address =
        facts->needs_saved_return_address || force_save_return_address;
    frame->return_address_offset = 0U;
    if (frame->saves_return_address) {
        if (!minic_core_frame_align_up(storage_size, 8U, &frame->return_address_offset) ||
            frame->return_address_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->return_address_offset + 8U;
    }

    frame->has_hidden_result_pointer = false;
    frame->hidden_result_pointer_offset = 0U;
    if (program != NULL) {
        MinicRiscv64FunctionAbiPlan local_plan = {0};
        MinicRiscv64FunctionAbiPlan *ready = abi_plan;
        MinicRiscv64AbiValue return_value;

        if (ready == NULL) {
            if (!minic_riscv64_function_abi_plan_build(
                    program, function, &local_plan))
                return false;
            ready = &local_plan;
        }
        if (!ready->prepared ||
            ready->parameter_location_count != function->parameter_count ||
            (function->parameter_count != 0U &&
             ready->parameter_locations == NULL)) {
            minic_riscv64_function_abi_plan_destroy(&local_plan);
            return false;
        }
        return_value = ready->return_value;
        frame->parameter_locations = ready->parameter_locations;
        frame->parameter_location_count = ready->parameter_location_count;
        frame->named_parameter_cursor = ready->named_parameter_cursor;
        frame->has_named_parameter_cursor = true;
        ready->parameter_locations = NULL;
        ready->parameter_location_count = 0U;
        ready->prepared = false;
        minic_riscv64_function_abi_plan_destroy(&local_plan);
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (!minic_type_is_record(function->return_type) ||
                return_value.storage_size <= 16U || return_value.slot_count != 1U ||
                !minic_core_frame_align_up(storage_size, 8U, &frame->hidden_result_pointer_offset) ||
                frame->hidden_result_pointer_offset > SIZE_MAX - 8U) {
                return false;
            }
            storage_size = frame->hidden_result_pointer_offset + 8U;
            frame->has_hidden_result_pointer = true;
        }
    }

    if (frame->has_dynamic_stack_alloc) {
        if (!minic_core_frame_align_up(storage_size, 8U, &frame->saved_frame_base_offset) ||
            frame->saved_frame_base_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->saved_frame_base_offset + 8U;
    }

    frame->preserves_structured_asm_callee_saved =
        facts->uses_structured_inline_asm;
    /*
     * Structured inline asm may explicitly bind/clobber s0. Dynamic-stack
     * functions reserve s0 as the stable fixed-frame base, so keep the mixed
     * case fail-closed until the asm allocator can reserve a frame register.
     */
    if (frame->has_dynamic_stack_alloc && frame->preserves_structured_asm_callee_saved) {
        return false;
    }
    frame->structured_asm_callee_saved_offset = 0U;
    if (frame->asm_plan.callee_saved_mask != 0U) {
        size_t saved_count =
            minic_riscv64_asm_callee_saved_mask_count(frame->asm_plan.callee_saved_mask);
        size_t saved_bytes = saved_count * 8U;
        if (!minic_core_frame_align_up(storage_size, 8U, &frame->structured_asm_callee_saved_offset) ||
            frame->structured_asm_callee_saved_offset > SIZE_MAX - saved_bytes) {
            return false;
        }
        storage_size = frame->structured_asm_callee_saved_offset + saved_bytes;
    }

    frame->stack_alignment = maximum_object_alignment;
    frame->has_dynamic_stack_alignment = maximum_object_alignment > 16U;
    frame->entry_sp_offset = 0U;
    if (frame->has_dynamic_stack_alignment) {
        if (!minic_core_frame_align_up(storage_size, 8U, &frame->entry_sp_offset) ||
            frame->entry_sp_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->entry_sp_offset + 8U;
    }

    frame->has_variadic_argument_address = (facts->variadic_address_count != 0U);
    frame->integer_parameter_count = 0U;
    frame->variadic_fixed_stack_slots = 0U;
    frame->varargs_size = 0U;
    if (frame->has_variadic_argument_address) {
        if (!frame->has_named_parameter_cursor ||
            !minic_riscv64_abi_variadic_prefix_from_cursor(
                &frame->named_parameter_cursor,
                &frame->integer_parameter_count,
                &frame->variadic_fixed_stack_slots)) {
            return false;
        }
        frame->varargs_size = (8U - frame->integer_parameter_count) * 8U;
    }
    if (storage_size > SIZE_MAX - frame->varargs_size) {
        return false;
    }
    required_size = storage_size + frame->varargs_size;
    if (!minic_core_frame_align_up(required_size, 16U, &frame->frame_size) ||
        frame->frame_size < frame->varargs_size) {
        return false;
    }
    frame->varargs_offset = frame->frame_size - frame->varargs_size;
    if (frame->varargs_offset < storage_size) {
        return false;
    }
    frame->object_count = function->object_count;
    frame->value_count = function->value_count;
    if (function->object_count != 0U) {
        size_t cached_offset;
        size_t cached_index;

        if (function->object_count > SIZE_MAX / sizeof(*frame->object_offsets)) {
            return false;
        }
        frame->object_offsets =
            (size_t *)malloc(function->object_count * sizeof(*frame->object_offsets));
        if (frame->object_offsets == NULL) {
            return false;
        }
        cached_offset = frame->outgoing_argument_size;
        for (cached_index = 0U; cached_index < function->object_count; ++cached_index) {
            const MinicCoreObjectStorage *object =
                &frame->object_storage.objects[cached_index];
            const size_t object_size = object->extent;
            const size_t object_alignment = object->alignment;
            if (cached_index <= UINT32_MAX) {
                bool scalar_reusable;
                size_t scalar_slot;
                size_t scalar_peak;
                if (!minic_core_object_reuse_plan_get(
                                                     &frame->object_reuse,
                                                     (MinicCoreObjectId)cached_index,
                                                     &scalar_reusable,
                                                     &scalar_slot,
                                                     &scalar_peak) ||
                    scalar_peak != scalar_reuse_pool_slots) {
                    free(frame->object_offsets);
                    frame->object_offsets = NULL;
                    return false;
                }
                if (scalar_reusable) {
                    size_t pool_bytes;
                    size_t pool_base;
                    if (scalar_slot >= scalar_reuse_pool_slots ||
                        scalar_reuse_pool_slots > frame->value_base_offset / 8U) {
                        free(frame->object_offsets);
                        frame->object_offsets = NULL;
                        return false;
                    }
                    pool_bytes = scalar_reuse_pool_slots * 8U;
                    if (pool_bytes > frame->value_base_offset) {
                        free(frame->object_offsets);
                        frame->object_offsets = NULL;
                        return false;
                    }
                    pool_base = frame->value_base_offset - pool_bytes;
                    frame->object_offsets[cached_index] = pool_base + scalar_slot * 8U;
                    continue;
                }
            }
            {
                MinicCoreObjectId reuse_candidate;
                if (cached_index <= UINT32_MAX &&
                    minic_riscv64_record_call_result_snapshot_reuse_candidate(
                        function, (MinicCoreObjectId)cached_index, &reuse_candidate)) {
                    if ((size_t)reuse_candidate >= cached_index) {
                        free(frame->object_offsets);
                        frame->object_offsets = NULL;
                        return false;
                    }
                    frame->object_offsets[cached_index] =
                        frame->object_offsets[(size_t)reuse_candidate];
                    continue;
                }
            }
            if (!minic_core_frame_align_up(cached_offset, object_alignment, &cached_offset)) {
                free(frame->object_offsets);
                frame->object_offsets = NULL;
                return false;
            }
            frame->object_offsets[cached_index] = cached_offset;
            if (cached_offset > SIZE_MAX - object_size) {
                free(frame->object_offsets);
                frame->object_offsets = NULL;
                return false;
            }
            cached_offset += object_size;
        }
    }
    /* Fail closed on missing SSA definitions as before, without rescanning
     * all basic blocks or reallocating the value-offset array. */
    if (function->value_count != 0U) {
        size_t value_index;
        for (value_index = 0U; value_index < function->value_count; ++value_index)
            if (frame->value_offsets[value_index] == SIZE_MAX)
                return false;
    }
    if (getenv("MINIC_CORE_FRAME_TRACE") != NULL) {
        (void)fprintf(stderr,
                      "CORE_FRAME_REUSE function=%s frame=%zu value_bytes=%zu values=%zu objects=%zu\n",
                      function->name != NULL ? function->name : "?",
                      frame->frame_size,
                      frame->value_slot_count * 8U,
                      function->value_count,
                      function->object_count);
    }
    return true;
}

void minic_riscv64_core_frame_destroy(MinicRiscv64CoreFrame *frame) {
    if (frame == NULL) {
        return;
    }
    minic_riscv64_asm_function_plan_destroy(&frame->asm_plan);
    minic_riscv64_call_plan_cache_destroy(&frame->outgoing_calls);
    minic_core_call_index_handle_destroy(&frame->call_index);
    frame->semantic_calls = NULL;
    minic_core_object_reuse_plan_destroy(&frame->object_reuse);
    free(frame->value_offsets);
    frame->value_offsets = NULL;
    free(frame->parameter_locations);
    frame->parameter_locations = NULL;
    frame->parameter_location_count = 0U;
    frame->has_named_parameter_cursor = false;
    free(frame->object_offsets);
    frame->object_offsets = NULL;
    minic_core_object_storage_plan_release(
        &frame->object_storage, frame->borrows_object_storage);
    free(frame->reachable_blocks);
    frame->reachable_blocks = NULL;
}

bool minic_riscv64_core_frame_parameter_location(
    const MinicRiscv64CoreFrame *frame, size_t parameter_index,
    MinicRiscv64AbiArgumentLocation *location) {
    if (frame == NULL || location == NULL || !frame->has_named_parameter_cursor ||
        parameter_index >= frame->parameter_location_count ||
        frame->parameter_locations == NULL)
        return false;
    *location = frame->parameter_locations[parameter_index];
    return true;
}

bool minic_riscv64_core_frame_object_offset(const MinicC0Program *program,
                               const MinicCoreFunction *function,
                               const MinicRiscv64CoreFrame *frame,
                               MinicCoreObjectId object_id,
                               size_t *offset) {
    (void)program;
    if (function == NULL || frame == NULL || offset == NULL ||
        frame->object_count != function->object_count ||
        object_id >= frame->object_count ||
        (frame->object_count != 0U && frame->object_offsets == NULL)) {
        return false;
    }
    *offset = frame->object_offsets[object_id];
    return true;
}

bool
minic_riscv64_core_frame_value_offset(const MinicRiscv64CoreFrame *frame, MinicCoreValueId value_id, size_t *offset) {
    if (frame == NULL || offset == NULL || value_id >= frame->value_count ||
        frame->value_offsets == NULL || frame->value_offsets[value_id] == SIZE_MAX) {
        return false;
    }
    *offset = frame->value_offsets[value_id];
    return true;
}

bool minic_riscv64_core_frame_mark_reachable_blocks(const MinicCoreFunction *function,
                                             MinicRiscv64CoreFrame *frame) {
    if (function == NULL || frame == NULL || function->block_count == 0U)
        return false;
    frame->reachable_blocks =
        (bool *)calloc(function->block_count, sizeof(*frame->reachable_blocks));
    return frame->reachable_blocks != NULL &&
           minic_core_function_mark_reachable_blocks(
               function, frame->reachable_blocks, function->block_count);
}

