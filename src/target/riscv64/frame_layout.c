#include "target/riscv64/frame_layout.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/spill_layout.h"
#include "target/riscv64/object_liveness.h"
#include "target/data_layout.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One RV64 owner of outgoing ABI, object reuse, spills and CFG planning. */
static bool align_up(size_t value, size_t alignment, size_t *result) {
    size_t remainder;

    if (result == NULL || alignment == 0U) {
        return false;
    }
    remainder = value % alignment;
    if (remainder == 0U) {
        *result = value;
        return true;
    }
    if (value > SIZE_MAX - (alignment - remainder)) {
        return false;
    }
    *result = value + (alignment - remainder);
    return true;
}

bool minic_riscv64_core_frame_initialize(const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  MinicRiscv64CoreFrame *frame) {
    size_t object_index;
    size_t storage_size;
    size_t required_size;
    size_t outgoing_argument_size;
    size_t maximum_object_alignment;
    size_t scalar_reuse_pool_slots;

    if (function == NULL || frame == NULL) {
        return false;
    }
    frame->object_offsets = NULL;
    frame->value_offsets = NULL;
    frame->reachable_blocks = NULL;
    frame->value_slot_count = 0U;
    frame->has_dynamic_stack_alloc = minic_riscv64_core_function_uses_dynamic_stack_alloc(function);
    frame->saved_frame_base_offset = 0U;
    if (!minic_riscv64_core_call_outgoing_stack_size(program, function, &outgoing_argument_size)) {
        return false;
    }
    frame->outgoing_argument_size = outgoing_argument_size;
    frame->dynamic_call_area_size = 0U;
    if (frame->has_dynamic_stack_alloc &&
        !align_up(outgoing_argument_size, 16U, &frame->dynamic_call_area_size)) {
        return false;
    }
    storage_size = outgoing_argument_size;
    maximum_object_alignment = 16U;
    if (!minic_riscv64_scalar_object_reuse_layout(program,
                                         function,
                                         MINIC_CORE_OBJECT_INVALID,
                                         NULL,
                                         NULL,
                                         &scalar_reuse_pool_slots)) {
        return false;
    }
    for (object_index = 0U; object_index < function->object_count; ++object_index) {
        size_t object_size;
        size_t object_alignment;

        if (!minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    function->objects[object_index].type,
                                    &object_size,
                                    &object_alignment) ||
            (object_size == 0U &&
             !minic_type_is_record(function->objects[object_index].type)) ||
            object_alignment == 0U ||
            (object_alignment & (object_alignment - 1U)) != 0U ||
            function->objects[object_index].element_count == 0U ||
            object_size > SIZE_MAX / function->objects[object_index].element_count) {
            return false;
        }
        if (function->objects[object_index].explicit_alignment != 0U) {
            size_t explicit_alignment = function->objects[object_index].explicit_alignment;

            if ((explicit_alignment & (explicit_alignment - 1U)) != 0U) {
                return false;
            }
            if (explicit_alignment > object_alignment) {
                object_alignment = explicit_alignment;
            }
        }
        if (object_alignment > maximum_object_alignment) {
            maximum_object_alignment = object_alignment;
        }
        object_size *= function->objects[object_index].element_count;
        if (object_index <= UINT32_MAX) {
            bool scalar_reusable;
            size_t scalar_slot;
            size_t scalar_peak;
            if (!minic_riscv64_scalar_object_reuse_layout(program,
                                                 function,
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
        if (!align_up(storage_size, object_alignment, &storage_size) ||
            storage_size > SIZE_MAX - object_size) {
            return false;
        }
        storage_size += object_size;
    }
    if (!align_up(storage_size, 8U, &storage_size) ||
        scalar_reuse_pool_slots > (SIZE_MAX - storage_size) / 8U) {
        return false;
    }
    storage_size += scalar_reuse_pool_slots * 8U;
    if (!align_up(storage_size, 8U, &frame->value_base_offset)) {
        return false;
    }
    /* Core block-local last-use reuse: compute the maximum physical spill
       extent needed by any one block. Distinct blocks still share the region. */
    {
        size_t block_index;
        size_t maximum_block_bytes = 0U;

        for (block_index = 0U; block_index < function->block_count; ++block_index) {
            size_t block_bytes;
            if (!minic_riscv64_block_reused_spill_layout(program,
                                                function,
                                                &function->blocks[block_index],
                                                frame->value_base_offset,
                                                NULL,
                                                &block_bytes)) {
                return false;
            }
            if (block_bytes > maximum_block_bytes) {
                maximum_block_bytes = block_bytes;
            }
        }
        if ((maximum_block_bytes & 7U) != 0U ||
            maximum_block_bytes > SIZE_MAX - frame->value_base_offset) {
            return false;
        }
        frame->value_slot_count = maximum_block_bytes / 8U;
        storage_size = frame->value_base_offset + maximum_block_bytes;
    }
    frame->saves_return_address = minic_riscv64_core_function_needs_saved_return_address(function);
    frame->return_address_offset = 0U;
    if (frame->saves_return_address) {
        if (!align_up(storage_size, 8U, &frame->return_address_offset) ||
            frame->return_address_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->return_address_offset + 8U;
    }

    frame->has_hidden_result_pointer = false;
    frame->hidden_result_pointer_offset = 0U;
    if (program != NULL) {
        MinicRiscv64AbiCursor return_cursor;
        MinicRiscv64AbiValue return_value;

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &return_cursor, &return_value)) {
            return false;
        }
        (void)return_cursor;
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (!minic_type_is_record(function->return_type) ||
                return_value.storage_size <= 16U || return_value.slot_count != 1U ||
                !align_up(storage_size, 8U, &frame->hidden_result_pointer_offset) ||
                frame->hidden_result_pointer_offset > SIZE_MAX - 8U) {
                return false;
            }
            storage_size = frame->hidden_result_pointer_offset + 8U;
            frame->has_hidden_result_pointer = true;
        }
    }

    if (frame->has_dynamic_stack_alloc) {
        if (!align_up(storage_size, 8U, &frame->saved_frame_base_offset) ||
            frame->saved_frame_base_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->saved_frame_base_offset + 8U;
    }

    frame->preserves_structured_asm_callee_saved =
        minic_riscv64_core_function_uses_structured_inline_asm(function);
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
        if (!align_up(storage_size, 8U, &frame->structured_asm_callee_saved_offset) ||
            frame->structured_asm_callee_saved_offset > SIZE_MAX - saved_bytes) {
            return false;
        }
        storage_size = frame->structured_asm_callee_saved_offset + saved_bytes;
    }

    frame->stack_alignment = maximum_object_alignment;
    frame->has_dynamic_stack_alignment = maximum_object_alignment > 16U;
    frame->entry_sp_offset = 0U;
    if (frame->has_dynamic_stack_alignment) {
        if (!align_up(storage_size, 8U, &frame->entry_sp_offset) ||
            frame->entry_sp_offset > SIZE_MAX - 8U) {
            return false;
        }
        storage_size = frame->entry_sp_offset + 8U;
    }

    frame->has_variadic_argument_address =
        minic_riscv64_core_function_uses_variadic_argument_address(function);
    frame->integer_parameter_count = 0U;
    frame->variadic_fixed_stack_slots = 0U;
    frame->varargs_size = 0U;
    if (frame->has_variadic_argument_address) {
        if (!minic_riscv64_core_variadic_fixed_prefix(program,
                                        function,
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
    if (!align_up(required_size, 16U, &frame->frame_size) ||
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
            size_t object_size;
            size_t object_alignment;

            if (!minic_data_layout_type(minic_default_data_layout(),
                                        program,
                                        function->objects[cached_index].type,
                                        &object_size,
                                        &object_alignment) ||
                object_alignment == 0U ||
                (object_alignment & (object_alignment - 1U)) != 0U ||
                function->objects[cached_index].element_count == 0U ||
                object_size > SIZE_MAX / function->objects[cached_index].element_count) {
                free(frame->object_offsets);
                frame->object_offsets = NULL;
                return false;
            }
            if (function->objects[cached_index].explicit_alignment != 0U &&
                function->objects[cached_index].explicit_alignment > object_alignment) {
                object_alignment = function->objects[cached_index].explicit_alignment;
            }
            if (cached_index <= UINT32_MAX) {
                bool scalar_reusable;
                size_t scalar_slot;
                size_t scalar_peak;
                if (!minic_riscv64_scalar_object_reuse_layout(program,
                                                     function,
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
            if (!align_up(cached_offset, object_alignment, &cached_offset)) {
                free(frame->object_offsets);
                frame->object_offsets = NULL;
                return false;
            }
            frame->object_offsets[cached_index] = cached_offset;
            object_size *= function->objects[cached_index].element_count;
            if (cached_offset > SIZE_MAX - object_size) {
                free(frame->object_offsets);
                frame->object_offsets = NULL;
                return false;
            }
            cached_offset += object_size;
        }
    }
    if (function->value_count != 0U) {
        size_t block_index;
        size_t value_index;

        if (function->value_count > SIZE_MAX / sizeof(*frame->value_offsets)) {
            free(frame->object_offsets);
            frame->object_offsets = NULL;
            return false;
        }
        frame->value_offsets =
            (size_t *)malloc(function->value_count * sizeof(*frame->value_offsets));
        if (frame->value_offsets == NULL) {
            free(frame->object_offsets);
            frame->object_offsets = NULL;
            return false;
        }
        for (value_index = 0U; value_index < function->value_count; ++value_index) {
            frame->value_offsets[value_index] = SIZE_MAX;
        }

        for (block_index = 0U; block_index < function->block_count; ++block_index) {
            size_t block_bytes;
            if (!minic_riscv64_block_reused_spill_layout(program,
                                                function,
                                                &function->blocks[block_index],
                                                frame->value_base_offset,
                                                frame->value_offsets,
                                                &block_bytes) ||
                block_bytes > frame->value_slot_count * 8U) {
                free(frame->value_offsets);
                frame->value_offsets = NULL;
                free(frame->object_offsets);
                frame->object_offsets = NULL;
                return false;
            }
        }
        for (value_index = 0U; value_index < function->value_count; ++value_index) {
            if (frame->value_offsets[value_index] == SIZE_MAX) {
                free(frame->value_offsets);
                frame->value_offsets = NULL;
                free(frame->object_offsets);
                frame->object_offsets = NULL;
                return false;
            }
        }
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
    minic_riscv64_object_liveness_end_function();
    free(frame->value_offsets);
    frame->value_offsets = NULL;
    free(frame->object_offsets);
    frame->object_offsets = NULL;
    free(frame->reachable_blocks);
    frame->reachable_blocks = NULL;
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

static bool
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
    MinicCoreBlockId *queue;
    size_t queue_begin;
    size_t queue_end;
    size_t block_index;

    if (function == NULL || frame == NULL || function->block_count == 0U ||
        function->entry_block >= function->block_count ||
        function->block_count > SIZE_MAX / sizeof(*queue)) {
        return false;
    }
    frame->reachable_blocks =
        (bool *)calloc(function->block_count, sizeof(*frame->reachable_blocks));
    queue = (MinicCoreBlockId *)malloc(function->block_count * sizeof(*queue));
    if (frame->reachable_blocks == NULL || queue == NULL) {
        free(queue);
        return false;
    }
    queue_begin = 0U;
    queue_end = 0U;
#define MINIC_CORE_REACHABLE_ENQUEUE(block_id_)                                      \
    do {                                                                             \
        MinicCoreBlockId reach_id_ = (block_id_);                                    \
        if (reach_id_ >= function->block_count) {                                    \
            free(queue);                                                              \
            return false;                                                             \
        }                                                                             \
        if (!frame->reachable_blocks[reach_id_]) {                                   \
            frame->reachable_blocks[reach_id_] = true;                               \
            queue[queue_end++] = reach_id_;                                           \
        }                                                                             \
    } while (0)

    MINIC_CORE_REACHABLE_ENQUEUE(function->entry_block);
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        if (function->blocks[block_index].source_label_id != SIZE_MAX) {
            MINIC_CORE_REACHABLE_ENQUEUE((MinicCoreBlockId)block_index);
        }
    }
    while (queue_begin < queue_end) {
        const MinicCoreBlock *block = &function->blocks[queue[queue_begin++]];

        if (!block->has_terminator) {
            free(queue);
            return false;
        }
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            MINIC_CORE_REACHABLE_ENQUEUE(block->terminator.branch_target);
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
            MINIC_CORE_REACHABLE_ENQUEUE(block->terminator.conditional.when_true);
            MINIC_CORE_REACHABLE_ENQUEUE(block->terminator.conditional.when_false);
            break;
        default:
            free(queue);
            return false;
        }
    }
#undef MINIC_CORE_REACHABLE_ENQUEUE
    free(queue);
    return true;
}

