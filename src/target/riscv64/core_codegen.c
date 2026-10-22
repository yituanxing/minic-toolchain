#include "target/riscv64/core_codegen.h"

#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/asm_plan.h"
#include "target/riscv64/frame_requirements.h"
#include "target/riscv64/spill_layout.h"
#include "target/riscv64/object_liveness.h"
#include "target/riscv64/legalize.h"
#include "core/value_uses.h"
#include "target/data_layout.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const minic_core_rv64_argument_registers[8] = {
    "a0",
    "a1",
    "a2",
    "a3",
    "a4",
    "a5",
    "a6",
    "a7",
};

static const char *const minic_core_rv64_floating_argument_registers[8] = {
    "fa0",
    "fa1",
    "fa2",
    "fa3",
    "fa4",
    "fa5",
    "fa6",
    "fa7",
};

/* M172_STRUCTURED_ASM_CALLEE_SAVED: basic-v0 conservatively preserves the
   complete RV64 callee-saved bank for functions containing structured asm.
   This lets asm operands fall back to s-registers when every caller-saved
   candidate is clobbered without making Core itself a register allocator. */
typedef struct MinicRiscv64CoreFrame {
    size_t frame_size;
    size_t object_count;
    size_t value_count;
    size_t value_slot_count;
    size_t *object_offsets;
    size_t *value_offsets;
    /* M177_CONSTANT_SWITCH_CFG_OWNER: suppress target emission of Core blocks
       that have no executable predecessor after semantic CFG pruning. */
    bool *reachable_blocks;
    size_t value_base_offset;
    size_t outgoing_argument_size;
    size_t dynamic_call_area_size;
    size_t return_address_offset;
    /* M167D_INDIRECT_RECORD_RETURN: psABI hidden result pointer is incoming
       state and must survive arbitrary calls before a Core RETURN. */
    size_t hidden_result_pointer_offset;
    size_t entry_sp_offset;
    size_t saved_frame_base_offset;
    size_t stack_alignment;
    size_t structured_asm_callee_saved_offset;
    MinicRiscv64AsmFunctionPlan asm_plan;
    size_t varargs_offset;
    size_t varargs_size;
    size_t integer_parameter_count;
    size_t variadic_fixed_stack_slots;
    bool saves_return_address;
    bool has_hidden_result_pointer;
    bool has_dynamic_stack_alignment;
    bool has_dynamic_stack_alloc;
    bool preserves_structured_asm_callee_saved;
    bool has_variadic_argument_address;
} MinicRiscv64CoreFrame;



static const MinicCoreFixedRegisterBinding *core_fixed_register_binding(
    const MinicCoreFunction *function, size_t binding_id) {
    return function != NULL && binding_id < function->fixed_register_binding_count
               ? &function->fixed_register_bindings[binding_id]
               : NULL;
}

static bool core_effective_integer_type(const MinicCoreFunction *function,
                                        MinicType type,
                                        MinicType *effective_type) {
    return minic_core_function_effective_integer_type(function, type, effective_type);
}

/* M74_GLOBAL_RECORD_ADDRESS / M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER:
   RV64 global.addr must keep assembler-local `.L*` symbols PC-relative in
   early position-independent code. Use `lla` only for `.L*` symbols; keep
   global/extern symbols on `la`.  The pointee storage shape is irrelevant
   until a later memory operation, even for declaration-only void symbols. */
static bool core_global_addressable_type(MinicType type) {
    return minic_riscv64_scalar_type(type) || minic_type_is_array(type) ||
           minic_type_is_record(type) || minic_type_is_void(type);
}

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

static bool core_frame_initialize(const MinicC0Program *program,
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

static void core_frame_destroy(MinicRiscv64CoreFrame *frame) {
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

static bool core_frame_fail(MinicRiscv64CoreFrame *frame) {
    core_frame_destroy(frame);
    return false;
}

static bool core_object_offset(const MinicC0Program *program,
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
core_value_offset(const MinicRiscv64CoreFrame *frame, MinicCoreValueId value_id, size_t *offset) {
    if (frame == NULL || offset == NULL || value_id >= frame->value_count ||
        frame->value_offsets == NULL || frame->value_offsets[value_id] == SIZE_MAX) {
        return false;
    }
    *offset = frame->value_offsets[value_id];
    return true;
}

static bool emit_sp_address(FILE *file, const char *destination_register, size_t offset) {
    if (file == NULL || destination_register == NULL) {
        return false;
    }
    if (offset <= 2047U) {
        return fprintf(file, "  addi %s, sp, %zu\n", destination_register, offset) >= 0;
    }
    return fprintf(file,
                   "  li t3, %zu\n"
                   "  add %s, sp, t3\n",
                   offset,
                   destination_register) >= 0;
}

static const char *core_frame_scratch_register(const char *value_register) {
    return value_register != NULL && strcmp(value_register, "t6") == 0 ? "t5" : "t6";
}

static bool emit_frame_address(FILE *file,
                               const MinicRiscv64CoreFrame *frame,
                               const char *destination_register,
                               size_t offset) {
    const char *scratch;

    if (file == NULL || frame == NULL || destination_register == NULL) {
        return false;
    }
    if (!frame->has_dynamic_stack_alloc) {
        return emit_sp_address(file, destination_register, offset);
    }
    if (offset <= 2047U) {
        return fprintf(file, "  addi %s, s0, %zu\n", destination_register, offset) >= 0;
    }
    scratch = core_frame_scratch_register(destination_register);
    return fprintf(file,
                   "  li %s, %zu\n"
                   "  add %s, s0, %s\n",
                   scratch,
                   offset,
                   destination_register,
                   scratch) >= 0;
}

static bool emit_frame_load64(FILE *file,
                              const MinicRiscv64CoreFrame *frame,
                              const char *destination_register,
                              size_t offset) {
    const char *scratch;

    if (file == NULL || frame == NULL || destination_register == NULL) {
        return false;
    }
    if (!frame->has_dynamic_stack_alloc) {
        return minic_riscv64_emit_sp_load64(file, destination_register, offset);
    }
    if (offset <= 2047U) {
        return fprintf(file, "  ld %s, %zu(s0)\n", destination_register, offset) >= 0;
    }
    scratch = core_frame_scratch_register(destination_register);
    return fprintf(file,
                   "  li %s, %zu\n"
                   "  add %s, s0, %s\n"
                   "  ld %s, 0(%s)\n",
                   scratch,
                   offset,
                   scratch,
                   scratch,
                   destination_register,
                   scratch) >= 0;
}

static bool emit_frame_store64(FILE *file,
                               const MinicRiscv64CoreFrame *frame,
                               const char *source_register,
                               size_t offset) {
    const char *scratch;

    if (file == NULL || frame == NULL || source_register == NULL) {
        return false;
    }
    if (!frame->has_dynamic_stack_alloc) {
        return minic_riscv64_emit_sp_store64(file, source_register, offset);
    }
    if (offset <= 2047U) {
        return fprintf(file, "  sd %s, %zu(s0)\n", source_register, offset) >= 0;
    }
    scratch = core_frame_scratch_register(source_register);
    return fprintf(file,
                   "  li %s, %zu\n"
                   "  add %s, s0, %s\n"
                   "  sd %s, 0(%s)\n",
                   scratch,
                   offset,
                   scratch,
                   scratch,
                   source_register,
                   scratch) >= 0;
}

static bool load_core_value(FILE *file,
                            const MinicRiscv64CoreFrame *frame,
                            MinicCoreValueId value_id,
                            const char *register_name) {
    size_t offset;

    return core_value_offset(frame, value_id, &offset) &&
           emit_frame_load64(file, frame, register_name, offset);
}

static bool store_core_value(FILE *file,
                             const MinicRiscv64CoreFrame *frame,
                             MinicCoreValueId value_id,
                             const char *register_name) {
    size_t offset;

    return core_value_offset(frame, value_id, &offset) &&
           emit_frame_store64(file, frame, register_name, offset);
}

/* M161_CORE_RV64_INT128_PAIR: Core remains target-neutral; RV64 lowers wide
   integer values to a low/high XLEN pair only inside the target backend. */
static bool load_core_int128_value(FILE *file,
                                   const MinicRiscv64CoreFrame *frame,
                                   MinicCoreValueId value_id,
                                   const char *low_register,
                                   const char *high_register) {
    size_t offset;

    return low_register != NULL && high_register != NULL &&
           core_value_offset(frame, value_id, &offset) && offset <= SIZE_MAX - 8U &&
           emit_frame_load64(file, frame, low_register, offset) &&
           emit_frame_load64(file, frame, high_register, offset + 8U);
}

static bool store_core_int128_value(FILE *file,
                                    const MinicRiscv64CoreFrame *frame,
                                    MinicCoreValueId value_id,
                                    const char *low_register,
                                    const char *high_register) {
    size_t offset;

    return low_register != NULL && high_register != NULL &&
           core_value_offset(frame, value_id, &offset) && offset <= SIZE_MAX - 8U &&
           emit_frame_store64(file, frame, low_register, offset) &&
           emit_frame_store64(file, frame, high_register, offset + 8U);
}

static bool core_opaque_inline_asm_supported(const MinicCoreFunction *function,
                                             const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM ||
        instruction->value.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    /* M89_EMPTY_VOLATILE_OPAQUE_ASM: opaque volatile asm may carry zero
       target bytes; emit_opaque_inline_asm naturally loops zero times. */
    if (inline_asm->template_text == NULL || !inline_asm->is_volatile) {
        return false;
    }
    /* M76_SINGLE_LABEL_ASM_GOTO: the target is explicit Core metadata even
       though the target-specific template remains opaque. */
    return !inline_asm->is_goto || inline_asm->goto_target < function->block_count;
}

static bool core_register_output_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM ||
        (!minic_type_is_integer(instruction->type) && !minic_type_is_pointer(instruction->type)) ||
        instruction->value.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            (inline_asm->template_text[index + 1U] != '%' &&
             inline_asm->template_text[index + 1U] != '0')) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool core_register_output_input_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicCoreValueId operand;
    size_t index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM ||
        (!minic_type_is_integer(instruction->type) && !minic_type_is_pointer(instruction->type)) ||
        instruction->value.register_output_input_inline_asm.inline_asm_id >=
            function->inline_asm_count) {
        return false;
    }
    operand = instruction->value.register_output_input_inline_asm.operand;
    if (operand >= function->value_count ||
        (!minic_type_is_integer(function->values[operand].type) &&
         !minic_type_is_pointer(function->values[operand].type))) {
        return false;
    }
    inline_asm = &function->inline_asms[
        instruction->value.register_output_input_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            (inline_asm->template_text[index + 1U] != '%' &&
             inline_asm->template_text[index + 1U] != '0' &&
             inline_asm->template_text[index + 1U] != '1')) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool core_memory_readwrite_scalar_input_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicCoreValueId memory_address;
    MinicCoreValueId operand;
    size_t memory_index;
    size_t register_index;
    size_t scalar_index;
    size_t index;
    bool has_register_output;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM ||
        instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id >=
            function->inline_asm_count) {
        return false;
    }
    memory_address = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address;
    operand = instruction->value.memory_readwrite_scalar_input_inline_asm.operand;
    memory_index = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index;
    register_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
    scalar_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index;
    has_register_output = register_index != SIZE_MAX;
    if (memory_address >= function->value_count || operand >= function->value_count ||
        !minic_type_is_pointer(function->values[memory_address].type) ||
        (!minic_type_is_integer(function->values[operand].type) &&
         !minic_type_is_pointer(function->values[operand].type)) ||
        memory_index > 9U || scalar_index > 9U || memory_index == scalar_index ||
        (has_register_output &&
         (register_index > 9U || register_index == memory_index || register_index == scalar_index))) {
        return false;
    }
    if (has_register_output) {
        if ((!minic_type_is_integer(instruction->type) &&
             !minic_type_is_pointer(instruction->type)) ||
            instruction->result == MINIC_CORE_VALUE_INVALID) {
            return false;
        }
    } else if (!minic_type_is_void(instruction->type) ||
               instruction->result != MINIC_CORE_VALUE_INVALID) {
        return false;
    }
    inline_asm = &function->inline_asms[
        instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile || !inline_asm->has_memory_clobber) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        size_t operand_index;
        unsigned char ch;

        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index + 1U];
        if (ch == '%') {
            index += 1U;
            continue;
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        operand_index = (size_t)(ch - '0');
        if (operand_index != memory_index && operand_index != scalar_index &&
            (!has_register_output || operand_index != register_index)) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool core_scalar_input_inline_asm_supported(
    const MinicCoreFunction *function, const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicCoreValueId operand;
    size_t index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM ||
        instruction->result != MINIC_CORE_VALUE_INVALID ||
        !minic_type_is_void(instruction->type) ||
        instruction->value.scalar_input_inline_asm.inline_asm_id >= function->inline_asm_count) {
        return false;
    }
    operand = instruction->value.scalar_input_inline_asm.operand;
    if (operand >= function->value_count ||
        (!minic_type_is_integer(function->values[operand].type) &&
         !minic_type_is_pointer(function->values[operand].type))) {
        return false;
    }
    inline_asm =
        &function->inline_asms[instruction->value.scalar_input_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || inline_asm->template_length == 0U ||
        !inline_asm->is_volatile) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            (inline_asm->template_text[index + 1U] != '%' &&
             inline_asm->template_text[index + 1U] != '0')) {
            return false;
        }
        index += 1U;
    }
    return true;
}

/* M126A_GENERIC_STRUCTURED_ASM: capability is now role/resource based. */
static bool core_structured_inline_asm_supported(const MinicCoreFunction *function,
                                                 const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    MinicRiscv64AsmInstructionPlan probe;
    bool bound[10] = {false};
    size_t binding_index;
    size_t template_index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        instruction->result != MINIC_CORE_VALUE_INVALID || !minic_type_is_void(instruction->type) ||
        instruction->value.structured_inline_asm.inline_asm_id >= function->inline_asm_count ||
        instruction->value.structured_inline_asm.operand_count == 0U ||
        instruction->value.structured_inline_asm.operand_count >
            MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];
    if (inline_asm->template_text == NULL || !inline_asm->is_volatile || inline_asm->is_goto) {
        return false;
    }
    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const MinicCoreFixedRegisterBinding *fixed_binding = NULL;
        MinicType pointee;
        MinicType value_type;

        if (binding->operand_index > 9U || bound[binding->operand_index] ||
            binding->value >= function->value_count ||
            (binding->early_clobber &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
             binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)) {
            return false;
        }
        bound[binding->operand_index] = true;
        if (binding->has_fixed_register_binding) {
            fixed_binding =
                core_fixed_register_binding(function, binding->fixed_register_binding_id);
            if (fixed_binding == NULL || !fixed_binding->is_local) {
                return false;
            }
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !minic_riscv64_scalar_type(value_type) ||
                (fixed_binding != NULL && !minic_type_equal(fixed_binding->type, value_type))) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
            if (fixed_binding != NULL ||
                !minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) || !minic_riscv64_scalar_type(value_type)) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!minic_riscv64_scalar_type(function->values[binding->value].type) ||
                (fixed_binding != NULL &&
                 !minic_type_equal(fixed_binding->type, function->values[binding->value].type))) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    for (template_index = 0U; template_index < inline_asm->template_length; ++template_index) {
        unsigned char ch;
        if (inline_asm->template_text[template_index] != '%') {
            continue;
        }
        if (++template_index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[template_index];
        if (ch == '%') {
            continue;
        }
        if (ch == 'z') {
            if (++template_index >= inline_asm->template_length) {
                return false;
            }
            ch = (unsigned char)inline_asm->template_text[template_index];
        }
        if (ch < '0' || ch > '9' || !bound[(size_t)(ch - '0')]) {
            return false;
        }
    }
    return minic_riscv64_asm_instruction_plan_build(function, instruction, &probe);
}

/* M85_RECORD_CALL_ARGUMENT: validate direct-call arguments against the
   shared RV64 ABI classifier. Core stores VALUE/OBJECT form only; register
   placement remains entirely target-owned. The first caller tier intentionally
   stays register-only, matching the pre-M85 Core call backend's no-stack scope. */
static bool core_instruction_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    if (function == NULL || instruction == NULL) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT:
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        return minic_riscv64_core_call_frame_address_supported(instruction);
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        return instruction->result < function->value_count &&
               instruction->value.operand < function->value_count &&
               minic_type_is_pointer(instruction->type) &&
               minic_type_equal(function->values[instruction->result].type, instruction->type) &&
               minic_type_equal(function->values[instruction->value.operand].type,
                                minic_type_unsigned_long());
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
        /* RV64 atomic ISA/memory-order lowering is not qualified yet.
         * Never map Core atomic.store to an ordinary unqualified store. */
        return false;
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
        /* This typed SysV variadic read must never silently reuse the
         * existing RV64 8-byte linear va_arg lowering. */
        return false;
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
        size_t fixed_stack_slots;
        size_t integer_parameter_count;

        return program != NULL && instruction->result < function->value_count &&
               minic_type_equal(function->values[instruction->result].type, instruction->type) &&
               minic_type_is_pointer(instruction->type) &&
               minic_riscv64_core_variadic_fixed_prefix(
                   program, function, &integer_parameter_count, &fixed_stack_slots);
    }
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
    case MINIC_CORE_INSTRUCTION_LOAD:
    case MINIC_CORE_INSTRUCTION_STORE:
        return true;
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        return minic_riscv64_core_record_load_supported(program, function, instruction, NULL);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return minic_riscv64_core_record_copy_supported(program, function, instruction);
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;

        binding = core_fixed_register_binding(
            function, instruction->value.fixed_register_binding_id);
        return binding != NULL && binding->register_name != NULL &&
               binding->register_name_length != 0U && minic_riscv64_scalar_type(binding->type) &&
               minic_type_equal(binding->type, instruction->type);
    }
    /* M64_LOCAL_LABEL_BLOCK_ADDRESS: RV64 spells the Core block label. */
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        return minic_type_is_pointer(instruction->type) && instruction->value.block_id < function->block_count;
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        return instruction->value.global_id < function->global_count &&
               function->globals[instruction->value.global_id].name != NULL &&
               function->globals[instruction->value.global_id].name_length != 0U;
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
        MinicType function_type;
        MinicCoreFunctionSymbolId symbol_id = instruction->value.function_symbol_id;
        return symbol_id < function->function_symbol_count &&
               function->function_symbols[symbol_id].name != NULL &&
               function->function_symbols[symbol_id].name_length != 0U &&
               minic_type_pointee(instruction->type, &function_type) &&
               minic_type_is_function(function_type);
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType result_type;
        size_t result_size;
        bool is_unsigned;

        return minic_riscv64_core_integer_overflow_supported(
            program, function, instruction, &result_type, &result_size, &is_unsigned);
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return core_opaque_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
        return core_register_output_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
        return core_register_output_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
        return core_memory_readwrite_scalar_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return core_scalar_input_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return core_structured_inline_asm_supported(function, instruction);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return minic_riscv64_core_direct_call_supported(program, function, instruction);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return minic_riscv64_core_indirect_call_supported(program, function, instruction);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return minic_riscv64_core_field_address_supported(instruction, NULL);
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        return minic_riscv64_core_scalar_bitcast_supported(program, function, instruction);
    }
    return false;
}

static bool core_rv64_capability_reject(const MinicCoreFunction *function,
                                        const char *stage,
                                        size_t index,
                                        int instruction_kind) {
    const char *trace;

    trace = getenv("CORE_FAST_TRACE");
    if (trace != NULL && trace[0] != '\0' && strcmp(trace, "0") != 0) {
        fprintf(stderr,
                "CORE_RV64_CAP function=%.*s stage=%s index=%zu instruction_kind=%d\n",
                function == NULL ? 0 : (int)function->name_length,
                function == NULL || function->name == NULL ? "" : function->name,
                stage == NULL ? "unknown" : stage,
                index,
                instruction_kind);
    }
    return false;
}

static bool core_function_can_emit(const MinicC0Program *program,
                                   const MinicCoreFunction *function) {
    size_t index;

    if (function == NULL || !minic_core_function_verify(function)) {
        return core_rv64_capability_reject(function, "verify", 0U, -1);
    }
    if (program == NULL) {
        if (!minic_type_is_void(function->return_type) &&
            !minic_riscv64_scalar_type(function->return_type)) {
            return core_rv64_capability_reject(function, "legacy-return", 0U, -1);
        }
        for (index = 0U; index < function->parameter_count; ++index) {
            if (!minic_riscv64_scalar_type(function->parameter_types[index])) {
                return core_rv64_capability_reject(function, "legacy-parameter", index, -1);
            }
        }
    } else {
        MinicRiscv64AbiCursor cursor;
        MinicRiscv64AbiValue return_value;

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &cursor, &return_value)) {
            return core_rv64_capability_reject(function, "return-abi", 0U, -1);
        }
        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
            if (return_value.slot_count == 0U || return_value.slot_count > 2U) {
                return core_rv64_capability_reject(function, "return-aggregate", 0U, -1);
            }
        } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (!minic_type_is_record(function->return_type) ||
                return_value.storage_size <= 16U || return_value.slot_count != 1U) {
                return core_rv64_capability_reject(function, "return-indirect", 0U, -1);
            }
        } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
            if (!minic_type_is_long_double(function->return_type) ||
                return_value.storage_size != 16U || return_value.slot_count != 2U) {
                return core_rv64_capability_reject(
                    function, "return-wide-scalar", 0U, -1);
            }
        } else if (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
                   return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                   !(return_value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
                     (minic_type_is_float(function->return_type) ||
                      minic_type_is_double(function->return_type)))) {
            return core_rv64_capability_reject(function, "return-kind", 0U, -1);
        }
        for (index = 0U; index < function->parameter_count; ++index) {
            MinicRiscv64AbiArgumentLocation location;

            if (!minic_riscv64_abi_place_argument(
                    program, function->parameter_types[index], true, &cursor, &location)) {
                return core_rv64_capability_reject(function, "parameter-abi", index, -1);
            }
            if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                if (!minic_type_is_record(function->parameter_types[index]) ||
                    location.value.storage_size == 0U || location.value.slot_count != 1U ||
                    location.floating_register_count != 0U ||
                    !((location.integer_register_count == 1U &&
                       location.integer_register_begin < 8U &&
                       location.stack_slot_count == 0U) ||
                      (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U))) {
                    return core_rv64_capability_reject(function, "parameter-indirect", index, -1);
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT) {
                if ((!minic_type_is_float(function->parameter_types[index]) &&
                     !minic_type_is_double(function->parameter_types[index])) ||
                    location.floating_register_count != 1U ||
                    location.floating_register_begin >= 8U ||
                    location.integer_register_count != 0U ||
                    location.stack_slot_count != 0U) {
                    return core_rv64_capability_reject(function, "parameter-float", index, -1);
                }
            } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
                if (!minic_type_is_long_double(function->parameter_types[index]) ||
                    location.value.storage_size != 16U || location.value.slot_count != 2U ||
                    location.floating_register_count != 0U ||
                    location.integer_register_count + location.stack_slot_count != 2U ||
                    location.integer_register_begin + location.integer_register_count > 8U) {
                    return core_rv64_capability_reject(
                        function, "parameter-wide-scalar", index, -1);
                }
            } else if (location.value.kind != MINIC_RISCV64_ABI_VALUE_IGNORE &&
                       location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                       (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                        location.value.slot_count == 0U || location.value.slot_count > 2U)) {
                return core_rv64_capability_reject(function, "parameter-kind", index, -1);
            }
        }
    }
    for (index = 0U; index < function->object_count; ++index) {
        size_t object_size;
        size_t object_alignment;
        MinicType object_type;

        object_type = function->objects[index].type;
        if ((!minic_riscv64_scalar_type(object_type) && !minic_type_is_record(object_type) &&
             !minic_type_is_array(object_type)) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    object_type,
                                    &object_size,
                                    &object_alignment) ||
            (object_size == 0U && !minic_type_is_record(object_type)) ||
            object_alignment == 0U ||
            (object_alignment & (object_alignment - 1U)) != 0U ||
            (function->objects[index].explicit_alignment != 0U &&
             (function->objects[index].explicit_alignment &
              (function->objects[index].explicit_alignment - 1U)) != 0U)) {
            return core_rv64_capability_reject(function, "object", index, -1);
        }
    }
    for (index = 0U; index < function->global_count; ++index) {
        if (function->globals[index].name == NULL || function->globals[index].name_length == 0U ||
            !core_global_addressable_type(function->globals[index].type)) {
            return core_rv64_capability_reject(function, "global", index, -1);
        }
    }
    for (index = 0U; index < function->value_count; ++index) {
        if (!minic_riscv64_scalar_type(function->values[index].type)) {
            return core_rv64_capability_reject(function, "value", index, -1);
        }
    }
    for (index = 0U; index < function->instruction_count; ++index) {
        if (!core_instruction_supported(program, function, &function->instructions[index])) {
            return core_rv64_capability_reject(
                function, "instruction", index, (int)function->instructions[index].kind);
        }
    }
    return true;
}

bool minic_riscv64_core_function_can_emit(const MinicCoreFunction *function) {
    return core_function_can_emit(NULL, function);
}

bool minic_riscv64_core_function_can_emit_for_program(const MinicC0Program *program,
                                                               const MinicCoreFunction *function) {
    return program != NULL && core_function_can_emit(program, function);
}

static bool core_parameter_location(const MinicC0Program *program,
                                    const MinicCoreFunction *function,
                                    size_t parameter_index,
                                    MinicRiscv64AbiArgumentLocation *location) {
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t index;

    if (program == NULL || function == NULL || location == NULL ||
        parameter_index >= function->parameter_count ||
        !minic_riscv64_abi_cursor_initialize_for_return(
            program, function->return_type, &cursor, &return_value)) {
        return false;
    }
    (void)return_value;
    for (index = 0U; index <= parameter_index; ++index) {
        MinicRiscv64AbiArgumentLocation current;

        if (!minic_riscv64_abi_place_argument(
                program, function->parameter_types[index], true, &cursor, &current)) {
            return false;
        }
        if (index == parameter_index) {
            *location = current;
        }
    }
    return true;
}

static bool emit_incoming_stack_load64(FILE *file,
                                               const MinicRiscv64CoreFrame *frame,
                                               const char *destination_register,
                                               size_t stack_slot) {
    size_t byte_offset;

    if (file == NULL || frame == NULL || destination_register == NULL ||
        stack_slot > SIZE_MAX / 8U) {
        return false;
    }
    byte_offset = stack_slot * 8U;
    if (!frame->has_dynamic_stack_alignment) {
        if (byte_offset > SIZE_MAX - frame->frame_size) {
            return false;
        }
        if (!frame->has_dynamic_stack_alloc) {
            return minic_riscv64_emit_sp_load64(
                file, destination_register, frame->frame_size + byte_offset);
        }
        if (frame->frame_size <= 2047U) {
            if (fprintf(file, "  addi t3, s0, %zu\n", frame->frame_size) < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  li t2, %zu\n"
                           "  add t3, s0, t2\n",
                           frame->frame_size) < 0) {
            return false;
        }
    } else if (!emit_frame_load64(file, frame, "t3", frame->entry_sp_offset)) {
        return false;
    }
    if (byte_offset <= 2047U) {
        return fprintf(file, "  ld %s, %zu(t3)\n", destination_register, byte_offset) >= 0;
    }
    return fprintf(file,
                   "  li t2, %zu\n"
                   "  add t3, t3, t2\n"
                   "  ld %s, 0(t3)\n",
                   byte_offset,
                   destination_register) >= 0;
}

static bool emit_parameter(FILE *file,
                           const MinicC0Program *program,
                           const MinicCoreFunction *function,
                           const MinicRiscv64CoreFrame *frame,
                           const MinicCoreInstruction *instruction) {
    size_t parameter_index;

    parameter_index = instruction->value.parameter_index;
    if (parameter_index >= function->parameter_count) {
        return false;
    }
    if (program != NULL) {
        MinicRiscv64AbiArgumentLocation location;

        if (!core_parameter_location(program, function, parameter_index, &location)) {
            return false;
        }
        if (location.value.kind == MINIC_RISCV64_ABI_VALUE_FLOAT &&
            (minic_type_is_float(instruction->type) ||
             minic_type_is_double(instruction->type))) {
            if (location.floating_register_count != 1U ||
                location.floating_register_begin >= 8U ||
                location.integer_register_count != 0U ||
                location.stack_slot_count != 0U) {
                return false;
            }
            if (minic_type_is_double(instruction->type)) {
                if (fprintf(file,
                            "  fmv.x.d t0, %s\n",
                            minic_core_rv64_floating_argument_registers[
                                location.floating_register_begin]) < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  fmv.x.w t0, %s\n"
                               "  slli t0, t0, 32\n"
                               "  srli t0, t0, 32\n",
                               minic_core_rv64_floating_argument_registers[
                                   location.floating_register_begin]) < 0) {
                return false;
            }
        } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR &&
                   minic_type_is_long_double(instruction->type)) {
            const char *chunk_registers[2] = {"t0", "t1"};
            size_t chunk_index;

            if (location.value.storage_size != 16U || location.value.slot_count != 2U ||
                location.floating_register_count != 0U ||
                location.integer_register_count + location.stack_slot_count != 2U) {
                return false;
            }
            for (chunk_index = 0U; chunk_index < 2U; ++chunk_index) {
                if (chunk_index < location.integer_register_count) {
                    size_t register_index = location.integer_register_begin + chunk_index;
                    if (register_index >= 8U ||
                        fprintf(file, "  mv %s, %s\n",
                                chunk_registers[chunk_index],
                                minic_core_rv64_argument_registers[register_index]) < 0) {
                        return false;
                    }
                } else {
                    size_t stack_chunk = chunk_index - location.integer_register_count;
                    if (stack_chunk >= location.stack_slot_count ||
                        !emit_incoming_stack_load64(
                            file, frame, chunk_registers[chunk_index],
                            location.stack_slot_begin + stack_chunk)) {
                        return false;
                    }
                }
            }
            return store_core_int128_value(
                file, frame, instruction->result, chunk_registers[0], chunk_registers[1]);
        } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INTEGER &&
                   location.floating_register_count == 0U) {
            if (location.integer_register_count == 1U && location.stack_slot_count == 0U &&
                location.integer_register_begin < 8U) {
                if (fprintf(file,
                            "  mv t0, %s\n",
                            minic_core_rv64_argument_registers[
                                location.integer_register_begin]) < 0) {
                    return false;
                }
            } else if (location.integer_register_count == 0U &&
                       location.stack_slot_count == 1U) {
                if (!emit_incoming_stack_load64(
                        file, frame, "t0", location.stack_slot_begin)) {
                    return false;
                }
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else if (parameter_index < 8U) {
        if (fprintf(file, "  mv t0, %s\n", minic_core_rv64_argument_registers[parameter_index]) <
            0) {
            return false;
        }
    } else {
        size_t stack_slot;

        stack_slot = parameter_index - 8U;
        if (!emit_incoming_stack_load64(file, frame, "t0", stack_slot)) {
            return false;
        }
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "t0")) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "t0");
}

static bool
emit_frame_load_chunk(FILE *file,
                      const MinicRiscv64CoreFrame *frame,
                      const char *destination_register,
                      size_t offset,
                      size_t size) {
    const char *opcode;
    size_t byte_index;

    if (file == NULL || frame == NULL || destination_register == NULL ||
        size == 0U || size > 8U) {
        return false;
    }
    if (size == 8U) {
        return emit_frame_load64(file, frame, destination_register, offset);
    }
    opcode = size == 4U ? "lwu" : size == 2U ? "lhu" : size == 1U ? "lbu" : NULL;
    if (opcode != NULL) {
        if (!emit_frame_address(file, frame, "t3", offset)) {
            return false;
        }
        return fprintf(file, "  %s %s, 0(t3)\n", opcode, destination_register) >= 0;
    }
    if (fprintf(file, "  li %s, 0\n", destination_register) < 0) {
        return false;
    }
    for (byte_index = 0U; byte_index < size; ++byte_index) {
        size_t byte_offset;

        if (offset > SIZE_MAX - byte_index) {
            return false;
        }
        byte_offset = offset + byte_index;
        if (!emit_frame_address(file, frame, "t3", byte_offset) ||
            fprintf(file, "  lbu t1, 0(t3)\n") < 0) {
            return false;
        }
        if (byte_index != 0U && fprintf(file, "  slli t1, t1, %zu\n", byte_index * 8U) < 0) {
            return false;
        }
        if (fprintf(file, "  or %s, %s, t1\n", destination_register, destination_register) < 0) {
            return false;
        }
    }
    return true;
}

static bool
emit_frame_store_chunk(FILE *file,
                       const MinicRiscv64CoreFrame *frame,
                       const char *source_register,
                       size_t offset,
                       size_t size) {
    const char *opcode;
    size_t byte_index;

    if (file == NULL || frame == NULL || source_register == NULL ||
        size == 0U || size > 8U) {
        return false;
    }
    if (size == 8U) {
        return emit_frame_store64(file, frame, source_register, offset);
    }
    opcode = size == 4U ? "sw" : size == 2U ? "sh" : size == 1U ? "sb" : NULL;
    if (opcode != NULL) {
        if (!emit_frame_address(file, frame, "t3", offset)) {
            return false;
        }
        return fprintf(file, "  %s %s, 0(t3)\n", opcode, source_register) >= 0;
    }
    if (fprintf(file, "  mv t1, %s\n", source_register) < 0) {
        return false;
    }
    for (byte_index = 0U; byte_index < size; ++byte_index) {
        size_t byte_offset;

        if (offset > SIZE_MAX - byte_index) {
            return false;
        }
        byte_offset = offset + byte_index;
        if (!emit_frame_address(file, frame, "t3", byte_offset) ||
            fprintf(file, "  sb t1, 0(t3)\n") < 0) {
            return false;
        }
        if (byte_index + 1U < size && fprintf(file, "  srli t1, t1, 8\n") < 0) {
            return false;
        }
    }
    return true;
}

static bool emit_parameter_object(FILE *file,
                                  const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  const MinicRiscv64CoreFrame *frame,
                                  const MinicCoreInstruction *instruction) {
    MinicRiscv64AbiArgumentLocation location;
    MinicCoreObjectId object_id;
    MinicType object_value_type;
    size_t object_offset;
    size_t chunk_index;

    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
        return false;
    }
    object_id = instruction->value.parameter_object.object_id;
    if (!core_parameter_location(
            program, function, instruction->value.parameter_object.parameter_index, &location) ||
        object_id >= function->object_count ||
        !minic_type_unqualified(function->objects[object_id].type, &object_value_type) ||
        !minic_type_equal(
            object_value_type,
            function->parameter_types[instruction->value.parameter_object.parameter_index]) ||
        !core_object_offset(program, function, frame, object_id, &object_offset)) {
        return false;
    }
    if (location.value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
        return location.value.slot_count == 0U && location.integer_register_count == 0U &&
               location.stack_slot_count == 0U;
    }
    if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        size_t copied;

        if (location.value.storage_size == 0U || location.value.slot_count != 1U ||
            location.floating_register_count != 0U) {
            return false;
        }
        if (location.integer_register_count == 1U && location.stack_slot_count == 0U) {
            size_t register_index = location.integer_register_begin;

            if (register_index >= 8U ||
                fprintf(file,
                        "  mv t1, %s\n",
                        minic_core_rv64_argument_registers[register_index]) < 0) {
                return false;
            }
        } else if (location.integer_register_count == 0U &&
                   location.stack_slot_count == 1U) {
            if (!emit_incoming_stack_load64(
                    file, frame, "t1", location.stack_slot_begin)) {
                return false;
            }
        } else {
            return false;
        }
        if (!emit_frame_address(file, frame, "t0", object_offset)) {
            return false;
        }
        copied = 0U;
        while (copied < location.value.storage_size) {
            size_t chunk = location.value.storage_size - copied;
            size_t offset;

            if (chunk > 2048U) {
                chunk = 2048U;
            }
            for (offset = 0U; offset < chunk; ++offset) {
                if (fprintf(file,
                            "  lbu t2, %zu(t1)\n"
                            "  sb t2, %zu(t0)\n",
                            offset,
                            offset) < 0) {
                    return false;
                }
            }
            copied += chunk;
            if (copied < location.value.storage_size &&
                fprintf(file,
                        "  addi t0, t0, 2047\n"
                        "  addi t0, t0, 1\n"
                        "  addi t1, t1, 2047\n"
                        "  addi t1, t1, 1\n") < 0) {
                return false;
            }
        }
        return true;
    }
    if (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
        location.value.slot_count == 0U || location.value.slot_count > 2U ||
        location.value.slot_count != location.integer_register_count + location.stack_slot_count) {
        return false;
    }
    for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
        const char *source_register;
        size_t chunk_offset;
        size_t chunk_size;

        source_register = "t0";
        if (chunk_index < location.integer_register_count) {
            size_t register_index;

            register_index = location.integer_register_begin + chunk_index;
            if (register_index >= 8U) {
                return false;
            }
            source_register = minic_core_rv64_argument_registers[register_index];
        } else {
            size_t stack_slot;

            stack_slot =
                location.stack_slot_begin + (chunk_index - location.integer_register_count);
            if (!emit_incoming_stack_load64(file, frame, "t0", stack_slot)) {
                return false;
            }
        }
        chunk_offset = chunk_index * 8U;
        if (chunk_offset >= location.value.storage_size ||
            object_offset > SIZE_MAX - chunk_offset) {
            return false;
        }
        chunk_size = location.value.storage_size - chunk_offset;
        if (chunk_size > 8U) {
            chunk_size = 8U;
        }
        if (!emit_frame_store_chunk(file, frame, source_register, object_offset + chunk_offset, chunk_size)) {
            return false;
        }
    }
    return true;
}

/* One RV64 psABI argument marshal for direct and indirect calls.
 * ABI cursor owns placement; this writer owns native transfer, including
 * 16-byte long-double splits, stack spills and partial record bytes.
 * Indirect-call unsupported shapes must remain fail-closed. */
static bool emit_core_call_argument(
    FILE *file, const MinicC0Program *program,
    const MinicCoreFunction *function, const MinicRiscv64CoreFrame *frame,
    const MinicCoreCallArgument *argument, MinicType argument_type,
    const MinicRiscv64AbiArgumentLocation *location,
    bool is_fixed_parameter, bool direct_call) {
    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        argument == NULL || location == NULL)
        return false;
    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
        if (location->value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR) {
            const char *chunk_registers[2] = {"t0", "t1"};
            size_t chunk_index;

            if (!direct_call || !minic_type_is_long_double(argument_type) ||
                location->value.storage_size != 16U || location->value.slot_count != 2U ||
                location->floating_register_count != 0U ||
                location->integer_register_count + location->stack_slot_count != 2U ||
                !load_core_int128_value(file, frame, argument->value.value_id,
                                        chunk_registers[0], chunk_registers[1])) {
                return false;
            }
            for (chunk_index = 0U; chunk_index < 2U; ++chunk_index) {
                if (chunk_index < location->integer_register_count) {
                    size_t register_index = location->integer_register_begin + chunk_index;
                    if (register_index >= 8U ||
                        fprintf(file, "  mv %s, %s\n",
                                minic_core_rv64_argument_registers[register_index],
                                chunk_registers[chunk_index]) < 0) {
                        return false;
                    }
                } else {
                    size_t stack_chunk = chunk_index - location->integer_register_count;
                    size_t stack_slot;
                    if (stack_chunk >= location->stack_slot_count ||
                        location->stack_slot_begin > SIZE_MAX - stack_chunk) {
                        return false;
                    }
                    stack_slot = location->stack_slot_begin + stack_chunk;
                    if (stack_slot > SIZE_MAX / 8U ||
                        !minic_riscv64_emit_sp_store64(
                            file, chunk_registers[chunk_index], stack_slot * 8U)) {
                        return false;
                    }
                }
            }
        } else if (location->floating_register_count != 0U) {
            const char *move_opcode;

            if (!is_fixed_parameter ||
                location->value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                (!minic_type_is_float(argument_type) &&
                 !minic_type_is_double(argument_type)) ||
                location->floating_register_count != 1U ||
                location->floating_register_begin >= 8U ||
                location->integer_register_count != 0U ||
                location->stack_slot_count != 0U ||
                !load_core_value(file, frame, argument->value.value_id, "t0")) {
                return false;
            }
            move_opcode = minic_type_is_double(argument_type) ? "fmv.d.x" : "fmv.w.x";
            if (fprintf(file,
                        "  %s %s, t0\n",
                        move_opcode,
                        minic_core_rv64_floating_argument_registers[
                            location->floating_register_begin]) < 0) {
                return false;
            }
        } else if (location->integer_register_count == 1U &&
                   location->stack_slot_count == 0U) {
            if (location->integer_register_begin >= 8U ||
                !load_core_value(
                    file,
                    frame,
                    argument->value.value_id,
                    minic_core_rv64_argument_registers[location->integer_register_begin])) {
                return false;
            }
        } else if (location->integer_register_count == 0U &&
                   location->stack_slot_count == 1U) {
            size_t outgoing_offset;

            if (location->stack_slot_begin > SIZE_MAX / 8U ||
                !load_core_value(file, frame, argument->value.value_id, "t0")) {
                return false;
            }
            outgoing_offset = location->stack_slot_begin * 8U;
            if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                return false;
            }
        } else {
            return false;
        }
        return true;
    }
    if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
        size_t chunk_index;
        size_t object_offset;

        if ((!direct_call && !is_fixed_parameter) ||
            !core_object_offset(
                program, function, frame, argument->value.object_id, &object_offset)) {
            return false;
        }
        if (location->value.kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
            if (location->value.slot_count != 0U || location->integer_register_count != 0U ||
                location->stack_slot_count != 0U) {
                return false;
            }
            return true;
        }
        if (location->value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            if (location->integer_register_count == 1U && location->stack_slot_count == 0U) {
                if (location->integer_register_begin >= 8U ||
                    !emit_frame_address(file,
                                        frame,
                                        minic_core_rv64_argument_registers[
                                         location->integer_register_begin],
                                     object_offset)) {
                    return false;
                }
            } else if (location->integer_register_count == 0U &&
                       location->stack_slot_count == 1U) {
                size_t outgoing_offset;
                if (location->stack_slot_begin > SIZE_MAX / 8U ||
                    !emit_frame_address(file, frame, "t0", object_offset)) {
                    return false;
                }
                outgoing_offset = location->stack_slot_begin * 8U;
                if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                    return false;
                }
            } else {
                return false;
            }
            return true;
        }
        if (location->value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
            return false;
        }
        for (chunk_index = 0U; chunk_index < location->value.slot_count; ++chunk_index) {
            size_t chunk_offset = chunk_index * 8U;
            size_t chunk_size;

            if (chunk_offset >= location->value.storage_size ||
                object_offset > SIZE_MAX - chunk_offset) {
                return false;
            }
            chunk_size = location->value.storage_size - chunk_offset;
            if (chunk_size > 8U) {
                chunk_size = 8U;
            }
            if (chunk_index < location->integer_register_count) {
                size_t register_index = location->integer_register_begin + chunk_index;
                if (register_index >= 8U ||
                    !emit_frame_load_chunk(file, frame,
                                        minic_core_rv64_argument_registers[register_index],
                                        object_offset + chunk_offset,
                                        chunk_size)) {
                    return false;
                }
            } else {
                size_t stack_chunk = chunk_index - location->integer_register_count;
                size_t stack_slot;
                size_t outgoing_offset;
                if (stack_chunk >= location->stack_slot_count ||
                    location->stack_slot_begin > SIZE_MAX - stack_chunk) {
                    return false;
                }
                stack_slot = location->stack_slot_begin + stack_chunk;
                if (stack_slot > SIZE_MAX / 8U ||
                    !emit_frame_load_chunk(
                        file, frame, "t0", object_offset + chunk_offset, chunk_size)) {
                    return false;
                }
                outgoing_offset = stack_slot * 8U;
                if (!minic_riscv64_emit_sp_store64(file, "t0", outgoing_offset)) {
                    return false;
                }
            }
        }
        return true;
    }
    return false;
}

/* The scalar return move is bit-for-bit identical for either callee
 * kind; record and binary128 returns keep their distinct ABI checks. */
static bool emit_core_call_scalar_result(
    FILE *file, const MinicC0Program *program,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction,
    const MinicRiscv64AbiValue *return_value) {
    if (file == NULL || program == NULL || frame == NULL ||
        instruction == NULL || return_value == NULL)
        return false;
    if (minic_type_is_float(instruction->type) ||
        minic_type_is_double(instruction->type)) {
        if (return_value->kind != MINIC_RISCV64_ABI_VALUE_FLOAT) {
            return false;
        }
        if (minic_type_is_double(instruction->type)) {
            if (fprintf(file, "  fmv.x.d a0, fa0\n") < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  fmv.x.w a0, fa0\n"
                           "  slli a0, a0, 32\n"
                           "  srli a0, a0, 32\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "a0");
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "a0")) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "a0");
}

static bool emit_call(FILE *file,
                      const MinicC0Program *program,
                      const MinicCoreFunction *function,
                      const MinicRiscv64CoreFrame *frame,
                      const MinicCoreInstruction *instruction) {
    const MinicCoreCallee *callee;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;

    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_CALL ||
        !minic_riscv64_core_direct_call_supported(program, function, instruction)) {
        return false;
    }
    callee = &function->callees[instruction->value.call.callee_id];
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value)) {
        return false;
    }
    if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
        size_t result_offset;

        if (instruction->value.call.result_object >= function->object_count ||
            !core_object_offset(
                program, function, frame, instruction->value.call.result_object, &result_offset) ||
            !emit_frame_address(file, frame, "a0", result_offset)) {
            return false;
        }
    }
    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType argument_type;
        bool is_fixed_parameter;
        if (!minic_riscv64_core_call_argument_type(
                function, argument, callee->parameter_types, callee->parameter_count,
                callee->is_variadic, argument_index, &argument_type,
                &is_fixed_parameter))
            return false;
        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location)) {
            return false;
        }
        if (!emit_core_call_argument(
                file, program, function, frame, argument, argument_type,
                &location, is_fixed_parameter, true))
            return false;
    }
    if (fprintf(file, "  call %s\n", callee->name) < 0) {
        return false;
    }
    if (minic_type_is_void(instruction->type)) {
        return true;
    }
    if (minic_type_is_record(instruction->type)) {
        size_t chunk_index;
        size_t object_offset;

        if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            return return_value.storage_size > 16U && return_value.slot_count == 1U &&
                   instruction->value.call.result_object < function->object_count;
        }
        if (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
            return_value.slot_count == 0U || return_value.slot_count > 2U ||
            instruction->value.call.result_object >= function->object_count ||
            !core_object_offset(
                program, function, frame, instruction->value.call.result_object, &object_offset)) {
            return false;
        }
        for (chunk_index = 0U; chunk_index < return_value.slot_count; ++chunk_index) {
            size_t chunk_offset = chunk_index * 8U;
            size_t chunk_size;
            const char *source_register =
                minic_core_rv64_argument_registers[chunk_index];

            if (chunk_offset >= return_value.storage_size ||
                object_offset > SIZE_MAX - chunk_offset) {
                return false;
            }
            chunk_size = return_value.storage_size - chunk_offset;
            if (chunk_size > 8U) {
                chunk_size = 8U;
            }
            if (!emit_frame_store_chunk(
                    file, frame, source_register, object_offset + chunk_offset, chunk_size)) {
                return false;
            }
        }
        return true;
    }
    if (minic_type_is_long_double(instruction->type)) {
        return return_value.kind == MINIC_RISCV64_ABI_VALUE_WIDE_SCALAR &&
               return_value.storage_size == 16U && return_value.slot_count == 2U &&
               store_core_int128_value(file, frame, instruction->result, "a0", "a1");
    }
    return emit_core_call_scalar_result(
        file, program, frame, instruction, &return_value);
}

static bool emit_indirect_call(FILE *file,
                               const MinicC0Program *program,
                               const MinicCoreFunction *function,
                               const MinicRiscv64CoreFrame *frame,
                               const MinicCoreInstruction *instruction) {
    const MinicCoreCallSignature *signature;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;

    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL ||
        !minic_riscv64_core_indirect_call_supported(program, function, instruction)) {
        return false;
    }
    signature = &function->call_signatures[instruction->value.indirect_call.signature_id];
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, signature->return_type, &cursor, &return_value)) {
        return false;
    }
    for (argument_index = 0U;
         argument_index < instruction->value.indirect_call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.indirect_call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType argument_type;
        bool is_fixed_parameter;
        if (!minic_riscv64_core_call_argument_type(
                function, argument, signature->parameter_types, signature->parameter_count,
                signature->is_variadic, argument_index, &argument_type,
                &is_fixed_parameter))
            return false;
        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location)) {
            return false;
        }
        if (!emit_core_call_argument(
                file, program, function, frame, argument, argument_type,
                &location, is_fixed_parameter, false))
            return false;
    }
    if (!load_core_value(file, frame, instruction->value.indirect_call.callee, "t0") ||
        fprintf(file, "  jalr ra, t0, 0\n") < 0) {
        return false;
    }
    if (minic_type_is_void(instruction->type)) {
        return true;
    }
    return emit_core_call_scalar_result(
        file, program, frame, instruction, &return_value);
}

static bool emit_field_address(FILE *file,
                               const MinicRiscv64CoreFrame *frame,
                               const MinicCoreInstruction *instruction) {
    size_t field_offset;

    if (!minic_riscv64_core_field_address_supported(instruction, &field_offset) ||
        !load_core_value(file, frame, instruction->value.field_address.base, "t0")) {
        return false;
    }
    if (field_offset != 0U) {
        if (field_offset <= 2047U) {
            if (fprintf(file, "  addi t0, t0, %zu\n", field_offset) < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  li t1, %zu\n"
                           "  add t0, t0, t1\n",
                           field_offset) < 0) {
            return false;
        }
    }
    return store_core_value(file, frame, instruction->result, "t0");
}

static bool emit_opaque_inline_asm(FILE *file,
                                   const MinicCoreFunction *function,
                                   const char *symbol_name,
                                   const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || !core_opaque_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (inline_asm->is_goto) {
        bool deferred_immediate = inline_asm->source_inline_asm_id != SIZE_MAX;

        if (symbol_name == NULL || symbol_name[0] == '\0' ||
            inline_asm->goto_target >= function->block_count) {
            return false;
        }
        if (deferred_immediate) {
            if (fprintf(file,
                        "  # MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization\n"
                        "  .extern __minic_deferred_asm_immediate_%zu_0\n"
                        "  ",
                        inline_asm->source_inline_asm_id) < 0) {
                return false;
            }
        } else if (fprintf(file, "  ") < 0) {
            return false;
        }
        for (index = 0U; index < inline_asm->template_length; ++index) {
            if (inline_asm->template_text[index] != '%') {
                if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                    return false;
                }
                continue;
            }
            if (index + 1U >= inline_asm->template_length) {
                return false;
            }
            if (inline_asm->template_text[index + 1U] == '%') {
                if (fputc('%', file) == EOF) {
                    return false;
                }
                index += 1U;
                continue;
            }
            if (inline_asm->template_text[index + 1U] == '0') {
                if (!deferred_immediate ||
                    fprintf(file,
                            "__minic_deferred_asm_immediate_%zu_0",
                            inline_asm->source_inline_asm_id) < 0) {
                    return false;
                }
                index += 1U;
                continue;
            }
            if (index + 2U < inline_asm->template_length &&
                inline_asm->template_text[index + 1U] == 'l' &&
                inline_asm->template_text[index + 2U] == '[') {
                size_t name_end = index + 3U;
                while (name_end < inline_asm->template_length &&
                       inline_asm->template_text[name_end] != ']') {
                    name_end += 1U;
                }
                if (name_end >= inline_asm->template_length || name_end == index + 3U ||
                    fprintf(file,
                            ".L%s_core_bb%" PRIu32,
                            symbol_name,
                            inline_asm->goto_target) < 0) {
                    return false;
                }
                index = name_end;
                continue;
            }
            return false;
        }
        return fputc('\n', file) != EOF;
    }
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (index + 1U >= inline_asm->template_length ||
            inline_asm->template_text[index + 1U] != '%') {
            return false;
        }
        if (fputc('%', file) == EOF) {
            return false;
        }
        index += 1U;
    }
    return fputc('\n', file) != EOF;
}

static bool emit_register_output_inline_asm(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || frame == NULL ||
        !core_register_output_inline_asm_supported(function, instruction)) {
        return false;
    }
    inline_asm = &function->inline_asms[instruction->value.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        if (inline_asm->template_text[index] == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '0') {
            if (fprintf(file, "t0") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (fputc('\n', file) == EOF ||
        (minic_type_is_integer(instruction->type) &&
         !minic_riscv64_emit_integer_conversion_for_program(
             file, program, instruction->type, "t0"))) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "t0");
}

static bool emit_register_output_input_inline_asm(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || frame == NULL ||
        !core_register_output_input_inline_asm_supported(function, instruction) ||
        !load_core_value(
            file, frame, instruction->value.register_output_input_inline_asm.operand, "t1")) {
        return false;
    }
    inline_asm = &function->inline_asms[
        instruction->value.register_output_input_inline_asm.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        if (inline_asm->template_text[index] == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '0') {
            if (fprintf(file, "t0") < 0) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '1') {
            if (fprintf(file, "t1") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (fputc('\n', file) == EOF ||
        (minic_type_is_integer(instruction->type) &&
         !minic_riscv64_emit_integer_conversion_for_program(
             file, program, instruction->type, "t0"))) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "t0");
}

static bool emit_memory_readwrite_scalar_input_inline_asm(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t memory_index;
    size_t register_index;
    size_t scalar_index;
    size_t index;
    bool has_register_output;

    if (file == NULL || program == NULL || frame == NULL ||
        !core_memory_readwrite_scalar_input_inline_asm_supported(function, instruction) ||
        !load_core_value(
            file,
            frame,
            instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address,
            "t0") ||
        !load_core_value(
            file, frame, instruction->value.memory_readwrite_scalar_input_inline_asm.operand, "t2")) {
        return false;
    }
    memory_index = instruction->value.memory_readwrite_scalar_input_inline_asm.memory_operand_index;
    register_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.register_output_operand_index;
    scalar_index =
        instruction->value.memory_readwrite_scalar_input_inline_asm.scalar_input_operand_index;
    has_register_output = register_index != SIZE_MAX;
    inline_asm = &function->inline_asms[
        instruction->value.memory_readwrite_scalar_input_inline_asm.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        size_t operand_index;
        unsigned char ch;

        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        operand_index = (size_t)(ch - '0');
        if (operand_index == memory_index) {
            if (fprintf(file, "(t0)") < 0) {
                return false;
            }
        } else if (has_register_output && operand_index == register_index) {
            if (fprintf(file, "t1") < 0) {
                return false;
            }
        } else if (operand_index == scalar_index) {
            if (fprintf(file, "t2") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    if (fputc('\n', file) == EOF) {
        return false;
    }
    if (!has_register_output) {
        return true;
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "t1")) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "t1");
}

static bool emit_scalar_input_inline_asm(
    FILE *file,
    const MinicCoreFunction *function,
    const MinicRiscv64CoreFrame *frame,
    const MinicCoreInstruction *instruction) {
    const MinicCoreInlineAsm *inline_asm;
    size_t index;

    if (file == NULL || frame == NULL ||
        !core_scalar_input_inline_asm_supported(function, instruction) ||
        !load_core_value(file, frame, instruction->value.scalar_input_inline_asm.operand, "t0")) {
        return false;
    }
    inline_asm =
        &function->inline_asms[instruction->value.scalar_input_inline_asm.inline_asm_id];
    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        index += 1U;
        if (inline_asm->template_text[index] == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
        } else if (inline_asm->template_text[index] == '0') {
            if (fprintf(file, "t0") < 0) {
                return false;
            }
        } else {
            return false;
        }
    }
    return fputc('\n', file) != EOF;
}

static bool emit_structured_inline_asm(FILE *file,
                                       const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicRiscv64CoreFrame *frame,
                                       const MinicCoreInstruction *instruction,
                                       size_t instruction_id) {
    const MinicCoreInlineAsm *inline_asm;
    const MinicRiscv64AsmInstructionPlan *plan;
    const char *const *operand_registers;
    const bool *memory_operand;
    const char *scratch_register;
    size_t binding_index;
    size_t index;

    if (file == NULL || program == NULL || frame == NULL || function == NULL ||
        instruction == NULL ||
        (plan = minic_riscv64_asm_function_plan_get(&frame->asm_plan, instruction_id)) == NULL)
        return false;
    operand_registers = plan->operand_registers;
    memory_operand = plan->memory_operand;
    scratch_register = plan->scratch_register;
    inline_asm = &function->inline_asms[instruction->value.structured_inline_asm.inline_asm_id];

    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name = operand_registers[binding->operand_index];
        MinicType pointee;
        MinicType value_type;

        if (register_name == NULL) {
            return false;
        }
        switch (binding->kind) {
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT:
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE:
            if (!load_core_value(file, frame, binding->value, scratch_register) ||
                !minic_type_pointee(function->values[binding->value].type, &pointee) ||
                !minic_type_unqualified(pointee, &value_type) ||
                !minic_riscv64_emit_scalar_load_for_program(
                    file, program, value_type, register_name, scratch_register)) {
                return false;
            }
            break;
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE:
        case MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT:
            if (!load_core_value(file, frame, binding->value, register_name)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }

    if (fprintf(file, "  ") < 0) {
        return false;
    }
    for (index = 0U; index < inline_asm->template_length; ++index) {
        unsigned char ch;
        size_t operand_index;
        if (inline_asm->template_text[index] != '%') {
            if (fputc((unsigned char)inline_asm->template_text[index], file) == EOF) {
                return false;
            }
            continue;
        }
        if (++index >= inline_asm->template_length) {
            return false;
        }
        ch = (unsigned char)inline_asm->template_text[index];
        if (ch == '%') {
            if (fputc('%', file) == EOF) {
                return false;
            }
            continue;
        }
        if (ch == 'z') {
            if (++index >= inline_asm->template_length) {
                return false;
            }
            ch = (unsigned char)inline_asm->template_text[index];
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        operand_index = (size_t)(ch - '0');
        if (operand_registers[operand_index] == NULL) {
            return false;
        }
        if (memory_operand[operand_index]) {
            if (fprintf(file, "(%s)", operand_registers[operand_index]) < 0) {
                return false;
            }
        } else if (fprintf(file, "%s", operand_registers[operand_index]) < 0) {
            return false;
        }
    }
    if (fputc('\n', file) == EOF) {
        return false;
    }

    for (binding_index = 0U;
         binding_index < instruction->value.structured_inline_asm.operand_count;
         ++binding_index) {
        const MinicCoreStructuredInlineAsmOperand *binding =
            &instruction->value.structured_inline_asm.operands[binding_index];
        const char *register_name;
        MinicType pointee;
        MinicType value_type;

        if (binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT &&
            binding->kind != MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE) {
            continue;
        }
        register_name = operand_registers[binding->operand_index];
        if (!minic_type_pointee(function->values[binding->value].type, &pointee) ||
            !minic_type_unqualified(pointee, &value_type) ||
            (minic_type_is_integer(value_type) &&
             !minic_riscv64_emit_integer_conversion_for_program(
                 file, program, value_type, register_name)) ||
            !load_core_value(file, frame, binding->value, scratch_register) ||
            !minic_riscv64_emit_scalar_store_for_program(
                file, program, value_type, register_name, scratch_register)) {
            return false;
        }
    }
    return true;
}

static bool emit_instruction(FILE *file,
                             const MinicC0Program *program,
                             const MinicCoreFunction *function,
                             const MinicRiscv64CoreFrame *frame,
                             const char *symbol_name,
                             const MinicCoreInstruction *instruction,
                             size_t instruction_id) {
    size_t object_offset;

    if (file == NULL || function == NULL || frame == NULL || symbol_name == NULL || instruction == NULL ||
        !core_instruction_supported(program, function, instruction)) {
        return false;
    }
    switch (instruction->kind) {
    case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR:
    case MINIC_CORE_INSTRUCTION_ATOMIC_STORE:
    case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD:
        return false;
    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
        if (fprintf(file, "  li t0, %" PRId64 "\n", instruction->value.integer_value) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
        if (minic_type_is_long_double(instruction->type)) {
            if (fprintf(file,
                        "  li t0, 0x%016" PRIx64 "\n"
                        "  li t1, 0x%016" PRIx64 "\n",
                        instruction->value.floating128_bits.low,
                        instruction->value.floating128_bits.high) < 0) {
                return false;
            }
            return store_core_int128_value(file, frame, instruction->result, "t0", "t1");
        }
        if (minic_type_is_double(instruction->type)) {
            if (fprintf(file,
                        "  li t0, 0x%016" PRIx64 "\n",
                        instruction->value.floating_bits) < 0) {
                return false;
            }
        } else if (minic_type_is_float(instruction->type)) {
            if (fprintf(file,
                        "  li t0, 0x%08" PRIx64 "\n",
                        instruction->value.floating_bits & UINT64_C(0xffffffff)) < 0) {
                return false;
            }
        } else {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
    case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE: {
        const char *opcode;

        if (!minic_type_is_float(instruction->type)) {
            return false;
        }
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
            opcode = "fadd.s";
            break;
        case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
            opcode = "fsub.s";
            break;
        case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
            opcode = "fmul.s";
            break;
        case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE:
            opcode = "fdiv.s";
            break;
        default:
            return false;
        }
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file,
                    "  fmv.w.x ft0, t0\n"
                    "  fmv.w.x ft1, t1\n"
                    "  %s ft0, ft0, ft1\n"
                    "  fmv.x.w t0, ft0\n"
                    "  slli t0, t0, 32\n"
                    "  srli t0, t0, 32\n",
                    opcode) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
    case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
    case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
    case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: {
        const char *opcode;

        if (!minic_type_is_double(instruction->type)) {
            return false;
        }
        switch (instruction->kind) {
        case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
            opcode = "fadd.d";
            break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
            opcode = "fsub.d";
            break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY:
            opcode = "fmul.d";
            break;
        case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE:
            opcode = "fdiv.d";
            break;
        default:
            return false;
        }
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fmv.d.x ft1, t1\n"
                    "  %s ft0, ft0, ft1\n"
                    "  fmv.x.d t0, ft0\n",
                    opcode) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
    case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
        const char *opcode;

        if (instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !minic_type_is_double(function->values[instruction->value.binary.left].type) ||
            !minic_type_equal(function->values[instruction->value.binary.left].type,
                              function->values[instruction->value.binary.right].type)) {
            return false;
        }
        opcode = instruction->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL
                     ? "feq.d"
                 : instruction->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS
                     ? "flt.d"
                     : "fle.d";
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fmv.d.x ft1, t1\n"
                    "  %s t0, ft0, ft1\n",
                    opcode) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  add t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  sub t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY:
        if (minic_type_is_int128_integer(instruction->type)) {
            MinicCoreValueId left = instruction->value.binary.left;
            MinicCoreValueId right = instruction->value.binary.right;

            if (left >= function->value_count || right >= function->value_count ||
                !minic_type_is_int128_integer(function->values[left].type) ||
                !minic_type_is_int128_integer(function->values[right].type) ||
                !load_core_int128_value(file, frame, left, "t0", "t1") ||
                !load_core_int128_value(file, frame, right, "t2", "t3") ||
                fprintf(file,
                        "  mulhu t4, t0, t2\n"
                        "  mul t5, t1, t2\n"
                        "  add t4, t4, t5\n"
                        "  mul t5, t0, t3\n"
                        "  add t4, t4, t5\n"
                        "  mul t0, t0, t2\n") < 0) {
                return false;
            }
            return store_core_int128_value(file, frame, instruction->result, "t0", "t4");
        }
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  mul t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
    case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
        MinicType effective_type;
        const char *opcode;

        if (!minic_core_function_effective_integer_type(function, instruction->type, &effective_type)) {
            return false;
        }
        if (instruction->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE) {
            opcode = minic_type_is_unsigned_integer(effective_type) ? "divu" : "div";
        } else {
            opcode = minic_type_is_unsigned_integer(effective_type) ? "remu" : "rem";
        }
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\n", opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  and t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  xor t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  or t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  sll t0, t0, t1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
        MinicType effective_type;
        const char *opcode;

        if (!minic_core_function_effective_integer_type(function, instruction->type, &effective_type)) {
            return false;
        }
        if (minic_type_is_int128_integer(instruction->type)) {
            bool is_signed;
            MinicCoreValueId left = instruction->value.binary.left;
            MinicCoreValueId right = instruction->value.binary.right;

            if (left >= function->value_count || right >= function->value_count ||
                !minic_type_is_int128_integer(function->values[left].type) ||
                !minic_type_is_integer(function->values[right].type) ||
                minic_type_is_int128_integer(function->values[right].type) ||
                !minic_riscv64_core_integer_type_is_signed(function, instruction->type, &is_signed) ||
                !load_core_int128_value(file, frame, left, "t0", "t1") ||
                !load_core_value(file, frame, right, "t2") ||
                fprintf(file,
                        "  beqz t2, .L%s_core_i128_shr_done_%" PRIu32 "\n"
                        "  li t3, 64\n"
                        "  bgeu t2, t3, .L%s_core_i128_shr_ge64_%" PRIu32 "\n"
                        "  neg t3, t2\n"
                        "  sll t4, t1, t3\n"
                        "  srl t0, t0, t2\n"
                        "  or t0, t0, t4\n"
                        "  %s t1, t1, t2\n"
                        "  j .L%s_core_i128_shr_done_%" PRIu32 "\n"
                        ".L%s_core_i128_shr_ge64_%" PRIu32 ":\n"
                        "  addi t2, t2, -64\n"
                        "  %s t0, t1, t2\n"
                        "  %s\n"
                        ".L%s_core_i128_shr_done_%" PRIu32 ":\n",
                        symbol_name,
                        instruction->result,
                        symbol_name,
                        instruction->result,
                        is_signed ? "sra" : "srl",
                        symbol_name,
                        instruction->result,
                        symbol_name,
                        instruction->result,
                        is_signed ? "sra" : "srl",
                        is_signed ? "  srai t1, t1, 63" : "  li t1, 0",
                        symbol_name,
                        instruction->result) < 0) {
                return false;
            }
            return store_core_int128_value(file, frame, instruction->result, "t0", "t1");
        }
        opcode = minic_type_is_unsigned_integer(effective_type) ? "srl" : "sra";
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\n", opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_LESS: {
        MinicType effective_type;
        MinicType operand_type;
        const char *opcode;

        if (instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count) {
            return false;
        }
        operand_type = function->values[instruction->value.binary.left].type;
        if (!minic_type_is_integer(operand_type) ||
            !minic_type_equal(operand_type,
                              function->values[instruction->value.binary.right].type) ||
            !minic_core_function_effective_integer_type(function, operand_type, &effective_type)) {
            return false;
        }
        opcode = minic_type_is_unsigned_integer(effective_type) ? "sltu" : "slt";
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  %s t0, t0, t1\n", opcode) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_POINTER_LESS:
        if (instruction->value.binary.left >= function->value_count ||
            instruction->value.binary.right >= function->value_count ||
            !minic_type_is_pointer(function->values[instruction->value.binary.left].type) ||
            !minic_type_equal(function->values[instruction->value.binary.left].type,
                              function->values[instruction->value.binary.right].type) ||
            !load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  sltu t0, t0, t1\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL:
        if (!load_core_value(file, frame, instruction->value.binary.left, "t0") ||
            !load_core_value(file, frame, instruction->value.binary.right, "t1") ||
            fprintf(file, "  xor t0, t0, t1\n  seqz t0, t0\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
        MinicType left_type;
        MinicType result_type;
        MinicType right_type;
        size_t result_size;
        bool is_unsigned;

        if (!minic_riscv64_core_integer_overflow_supported(
                program, function, instruction, &result_type, &result_size, &is_unsigned) ||
            !load_core_value(file, frame, instruction->value.integer_overflow.left, "t0") ||
            !load_core_value(file, frame, instruction->value.integer_overflow.right, "t1") ||
            !load_core_value(
                file, frame, instruction->value.integer_overflow.result_address, "t3")) {
            return false;
        }
        left_type = function->values[instruction->value.integer_overflow.left].type;
        right_type = function->values[instruction->value.integer_overflow.right].type;
        if ((!minic_type_equal(left_type, result_type) ||
             !minic_type_equal(right_type, result_type)) &&
            !(minic_riscv64_core_integer_type_range_fits(program, function, left_type, result_type) &&
              minic_riscv64_core_integer_type_range_fits(program, function, right_type, result_type)) &&
            !minic_riscv64_core_integer_overflow_xlen_scratch_exact(
                program, left_type, right_type, result_size)) {
            const char *signed_register;
            const char *unsigned_register;
            uint64_t maximum;

            if (is_unsigned || result_size >= 8U) {
                return false;
            }
            if (instruction->value.integer_overflow.operator_kind ==
                MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
                if (fprintf(file, "  mul t2, t0, t1\n  mv t4, t2\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0 ||
                    !minic_riscv64_emit_scalar_store_for_program(
                        file, program, result_type, "t2", "t3")) {
                    return false;
                }
                return store_core_value(file, frame, instruction->result, "t4");
            }
            if (instruction->value.integer_overflow.operator_kind !=
                MINIC_CORE_INTEGER_OVERFLOW_ADD) {
                return false;
            }
            signed_register = minic_type_equal(left_type, result_type) ? "t0" : "t1";
            unsigned_register = minic_type_equal(left_type, result_type) ? "t1" : "t0";
            maximum = (UINT64_C(1) << (result_size * 8U - 1U)) - UINT64_C(1);
            if (fprintf(file,
                        "  li t5, %" PRIu64 "\n"
                        "  sub t5, t5, %s\n"
                        "  sltu t4, t5, %s\n"
                        "  add t2, t0, t1\n",
                        maximum,
                        signed_register,
                        unsigned_register) < 0 ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, result_type, "t2") ||
                !minic_riscv64_emit_scalar_store_for_program(
                    file, program, result_type, "t2", "t3")) {
                return false;
            }
            return store_core_value(file, frame, instruction->result, "t4");
        }

        if (instruction->value.integer_overflow.operator_kind == MINIC_CORE_INTEGER_OVERFLOW_ADD) {
            if (result_size < 8U) {
                if (fprintf(file, "  add t2, t0, t1\n  mv t4, t2\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0) {
                    return false;
                }
            } else if (is_unsigned) {
                if (fprintf(file,
                            "  add t2, t0, t1\n"
                            "  sltu t4, t2, t0\n") < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  add t2, t0, t1\n"
                               "  xor t4, t0, t1\n"
                               "  xori t4, t4, -1\n"
                               "  xor t5, t0, t2\n"
                               "  and t4, t4, t5\n"
                               "  srli t4, t4, 63\n") < 0) {
                return false;
            }
        } else if (instruction->value.integer_overflow.operator_kind ==
                   MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
            if (result_size < 8U) {
                if (fprintf(file, "  sub t2, t0, t1\n  mv t4, t2\n") < 0 ||
                    !minic_riscv64_emit_integer_conversion_for_program(
                        file, program, result_type, "t2") ||
                    fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0) {
                    return false;
                }
            } else if (is_unsigned) {
                if (fprintf(file,
                            "  sub t2, t0, t1\n"
                            "  sltu t4, t0, t1\n") < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  sub t2, t0, t1\n"
                               "  xor t4, t0, t1\n"
                               "  xor t5, t0, t2\n"
                               "  and t4, t4, t5\n"
                               "  srli t4, t4, 63\n") < 0) {
                return false;
            }
        } else if (result_size < 8U) {
            if (fprintf(file, "  mul t2, t0, t1\n  mv t4, t2\n") < 0 ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, result_type, "t2") ||
                fprintf(file, "  xor t4, t4, t2\n  snez t4, t4\n") < 0) {
                return false;
            }
        } else if (is_unsigned) {
            if (fprintf(file,
                        "  mul t2, t0, t1\n"
                        "  mulhu t4, t0, t1\n"
                        "  snez t4, t4\n") < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  mul t2, t0, t1\n"
                           "  mulh t4, t0, t1\n"
                           "  srai t5, t2, 63\n"
                           "  xor t4, t4, t5\n"
                           "  snez t4, t4\n") < 0) {
            return false;
        }
        if (!minic_riscv64_emit_scalar_store_for_program(file, program, result_type, "t2", "t3")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t4");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType source_type;

        if (operand >= function->value_count) {
            return false;
        }
        source_type = function->values[operand].type;
        if (minic_type_is_int128_integer(instruction->type)) {
            if (minic_type_is_int128_integer(source_type)) {
                if (!load_core_int128_value(file, frame, operand, "t0", "t1")) {
                    return false;
                }
            } else {
                bool source_signed;

                if (!minic_type_is_integer(source_type) ||
                    !load_core_value(file, frame, operand, "t0") ||
                    !minic_riscv64_core_integer_type_is_signed(function, source_type, &source_signed) ||
                    fprintf(file,
                            source_signed ? "  srai t1, t0, 63\n" : "  li t1, 0\n") < 0) {
                    return false;
                }
            }
            return store_core_int128_value(file, frame, instruction->result, "t0", "t1");
        }
        if (minic_type_is_int128_integer(source_type)) {
            if (!load_core_int128_value(file, frame, operand, "t0", "t1") ||
                !minic_riscv64_emit_integer_conversion_for_program(
                    file, program, instruction->type, "t0")) {
                return false;
            }
            return store_core_value(file, frame, instruction->result, "t0");
        }
        if (!load_core_value(file, frame, operand, "t0") ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType source_type;
        const char *opcode;

        if (operand >= function->value_count ||
            !core_effective_integer_type(function, function->values[operand].type, &source_type) ||
            minic_type_is_int128_integer(source_type) ||
            !minic_type_is_double(instruction->type)) {
            return false;
        }
        if (minic_type_is_long_integer(source_type)) {
            opcode = minic_type_is_unsigned_integer(source_type) ? "fcvt.d.lu" : "fcvt.d.l";
        } else {
            opcode = minic_type_is_unsigned_integer(source_type) ? "fcvt.d.wu" : "fcvt.d.w";
        }
        if (!load_core_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  %s ft0, t0\n"
                    "  fmv.x.d t1, ft0\n",
                    opcode) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE: {
        MinicCoreValueId operand = instruction->value.operand;

        if (operand >= function->value_count ||
            !minic_type_is_float(function->values[operand].type) ||
            !minic_type_is_double(instruction->type) ||
            !load_core_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.w.x ft0, t0\n"
                    "  fcvt.d.s ft1, ft0\n"
                    "  fmv.x.d t1, ft1\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
        MinicCoreValueId operand = instruction->value.operand;

        if (operand >= function->value_count ||
            !minic_type_is_double(function->values[operand].type) ||
            !minic_type_is_float(instruction->type) ||
            !load_core_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fcvt.s.d ft1, ft0\n"
                    "  fmv.x.w t1, ft1\n"
                    "  slli t1, t1, 32\n"
                    "  srli t1, t1, 32\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
        MinicCoreValueId operand = instruction->value.operand;
        MinicType target_type;
        const char *opcode;

        if (operand >= function->value_count ||
            !minic_type_is_double(function->values[operand].type) ||
            !core_effective_integer_type(function, instruction->type, &target_type) ||
            minic_type_is_int128_integer(target_type)) {
            return false;
        }
        if (minic_type_is_long_integer(target_type)) {
            opcode = minic_type_is_unsigned_integer(target_type) ? "fcvt.lu.d" : "fcvt.l.d";
        } else {
            opcode = minic_type_is_unsigned_integer(target_type) ? "fcvt.wu.d" : "fcvt.w.d";
        }
        if (!load_core_value(file, frame, operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  %s t1, ft0, rtz\n",
                    opcode) < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t1")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
        if (!load_core_value(file, frame, instruction->value.operand, "t0")) {
            return false;
        }
        if (minic_type_is_integer(instruction->type) &&
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  fmv.d.x ft0, t0\n"
                    "  fsgnjn.d ft0, ft0, ft0\n"
                    "  fmv.x.d t0, ft0\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  neg t0, t0\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  xori t0, t0, -1\n") < 0 ||
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ: {
        unsigned int width;

        if (!minic_riscv64_core_unsigned_integer_width(
                program, function, instruction->value.operand, &width) ||
            !load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  li t1, 0\n"
                    "  li t2, 1\n"
                    "  slli t2, t2, %u\n"
                    ".L%s_core_clz_loop_%" PRIu32 ":\n"
                    "  and t3, t0, t2\n"
                    "  bnez t3, .L%s_core_clz_done_%" PRIu32 "\n"
                    "  addi t1, t1, 1\n"
                    "  srli t2, t2, 1\n"
                    "  bnez t2, .L%s_core_clz_loop_%" PRIu32 "\n"
                    ".L%s_core_clz_done_%" PRIu32 ":\n",
                    width - 1U,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
        unsigned int width;

        if (!minic_riscv64_core_unsigned_integer_width(
                program, function, instruction->value.operand, &width) ||
            !load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  beqz t0, .L%s_core_ctz_zero_%" PRIu32 "\n"
                    "  li t1, 0\n"
                    ".L%s_core_ctz_loop_%" PRIu32 ":\n"
                    "  andi t2, t0, 1\n"
                    "  bnez t2, .L%s_core_ctz_done_%" PRIu32 "\n"
                    "  addi t1, t1, 1\n"
                    "  srli t0, t0, 1\n"
                    "  j .L%s_core_ctz_loop_%" PRIu32 "\n"
                    ".L%s_core_ctz_zero_%" PRIu32 ":\n"
                    "  li t1, %u\n"
                    ".L%s_core_ctz_done_%" PRIu32 ":\n",
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    symbol_name, instruction->result,
                    width,
                    symbol_name, instruction->result) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    }
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
        if (!load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file, "  seqz t0, t0\n") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        if (!load_core_value(file, frame, instruction->value.pointer_offset.base, "t0") ||
            !load_core_value(file, frame, instruction->value.pointer_offset.index, "t1")) {
            return false;
        }
        if (instruction->value.pointer_offset.element_size != 1U &&
            fprintf(file,
                    "  li t2, %zu\n"
                    "  mul t1, t1, t2\n",
                    instruction->value.pointer_offset.element_size) < 0) {
            return false;
        }
        /* M75_POINTER_COMPOUND_ASSIGNMENT_VALUE: retain subtraction through
           the Core boundary so an unsigned index is scaled before `sub`. */
        if (fprintf(file,
                    "  %s t0, t0, t1\n",
                    instruction->value.pointer_offset.subtract ? "sub" : "add") < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ: {
        const MinicCoreFixedRegisterBinding *binding;

        binding = core_fixed_register_binding(
            function, instruction->value.fixed_register_binding_id);
        if (binding == NULL || binding->register_name == NULL ||
            binding->register_name_length == 0U || !minic_riscv64_scalar_type(binding->type) ||
            !minic_type_equal(binding->type, instruction->type) ||
            fprintf(file, "  mv t0, %s\n", binding->register_name) < 0) {
            return false;
        }
        if (minic_type_is_integer(instruction->type) &&
            !minic_riscv64_emit_integer_conversion_for_program(
                file, program, instruction->type, "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    }
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
        if (!minic_riscv64_core_call_frame_address_supported(instruction)) {
            return false;
        }
        if (instruction->value.call_frame_address.kind == MINIC_CORE_CALL_FRAME_ADDRESS_RETURN) {
            if (!frame->saves_return_address ||
                !emit_frame_load64(file, frame, "t0", frame->return_address_offset)) {
                return false;
            }
        } else if (instruction->value.call_frame_address.kind ==
                   MINIC_CORE_CALL_FRAME_ADDRESS_FRAME) {
            if (fprintf(file,
                        frame->has_dynamic_stack_alloc ? "  mv t0, s0\n" : "  mv t0, sp\n") < 0) {
                return false;
            }
        } else {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        if (!frame->has_dynamic_stack_alloc ||
            !load_core_value(file, frame, instruction->value.operand, "t0") ||
            fprintf(file,
                    "  addi t0, t0, 15\n"
                    "  li t1, -16\n"
                    "  and t0, t0, t1\n"
                    "  sub sp, sp, t0\n") < 0) {
            return false;
        }
        if (frame->dynamic_call_area_size == 0U) {
            if (fprintf(file, "  mv t0, sp\n") < 0) {
                return false;
            }
        } else if (frame->dynamic_call_area_size <= 2047U) {
            if (fprintf(file,
                        "  addi t0, sp, %zu\n",
                        frame->dynamic_call_area_size) < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  li t0, %zu\n"
                           "  add t0, sp, t0\n",
                           frame->dynamic_call_area_size) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        if (!frame->has_variadic_argument_address) {
            return false;
        }
        if (frame->varargs_size != 0U) {
            if (!emit_frame_address(file, frame, "t0", frame->varargs_offset)) {
                return false;
            }
        } else {
            size_t stack_byte_offset;

            if (frame->variadic_fixed_stack_slots > SIZE_MAX / 8U) {
                return false;
            }
            stack_byte_offset = frame->variadic_fixed_stack_slots * 8U;
            if (frame->has_dynamic_stack_alignment) {
                if (!emit_frame_load64(file, frame, "t0", frame->entry_sp_offset)) {
                    return false;
                }
            } else {
                if (frame->frame_size <= 2047U) {
                    if (fprintf(file,
                                frame->has_dynamic_stack_alloc
                                    ? "  addi t0, s0, %zu\n"
                                    : "  addi t0, sp, %zu\n",
                                frame->frame_size) < 0) {
                        return false;
                    }
                } else if (fprintf(file,
                                   frame->has_dynamic_stack_alloc
                                       ? "  li t0, %zu\n  add t0, s0, t0\n"
                                       : "  li t0, %zu\n  add t0, sp, t0\n",
                                   frame->frame_size) < 0) {
                    return false;
                }
            }
            if (stack_byte_offset != 0U) {
                if (stack_byte_offset <= 2047U) {
                    if (fprintf(file, "  addi t0, t0, %zu\n", stack_byte_offset) < 0) {
                        return false;
                    }
                } else if (fprintf(file,
                                   "  li t1, %zu\n"
                                   "  add t0, t0, t1\n",
                                   stack_byte_offset) < 0) {
                    return false;
                }
            }
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return emit_parameter(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return emit_parameter_object(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
        if (!core_object_offset(program, function, frame, instruction->value.object_id, &object_offset) ||
            !emit_frame_address(file, frame, "t0", object_offset)) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
        if (instruction->value.block_id >= function->block_count ||
            fprintf(file, "  la t0, .L%s_core_bb%" PRIu32 "\n", symbol_name, instruction->value.block_id) < 0) return false;
        return store_core_value(file, frame, instruction->result, "t0");
    /* M196_LINUX_STATIC_PCREL_GLOBAL_ADDRESS: the Linux kernel is statically
       linked and executes early global accesses before the MMU is enabled.
       Use the explicit PC-relative pseudo-op so GAS cannot choose GOT form. */
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
        if (instruction->value.global_id >= function->global_count ||
            fprintf(file, "  lla t0, %s\n", function->globals[instruction->value.global_id].name) <
                0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    /* M197_LINUX_STATIC_PCREL_FUNCTION_ADDRESS: early static-kernel code may
       take function addresses while MMU-off (for example set_satp_mode).
       Keep those addresses PC-relative rather than GOT/high-half based. */
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
        if (instruction->value.function_symbol_id >= function->function_symbol_count ||
            fprintf(file,
                    "  lla t0, %s\n",
                    function->function_symbols[instruction->value.function_symbol_id].name) < 0) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t0");
    case MINIC_CORE_INSTRUCTION_LOAD:
        if (minic_type_is_int128_integer(instruction->type) ||
            minic_type_is_long_double(instruction->type)) {
            if (!load_core_value(file, frame, instruction->value.load.address, "t0") ||
                fprintf(file, "  ld t1, 0(t0)\n  ld t2, 8(t0)\n") < 0) {
                return false;
            }
            return store_core_int128_value(file, frame, instruction->result, "t1", "t2");
        }
        if (!load_core_value(file, frame, instruction->value.load.address, "t0") ||
            !minic_riscv64_emit_scalar_load_for_program(
                file, program, instruction->type, "t1", "t0")) {
            return false;
        }
        return store_core_value(file, frame, instruction->result, "t1");
    case MINIC_CORE_INSTRUCTION_STORE: {
        MinicCoreValueId stored_value;
        MinicType stored_type;

        stored_value = instruction->value.store.stored_value;
        if (stored_value >= function->value_count) {
            return false;
        }
        stored_type = function->values[stored_value].type;
        if (minic_type_is_int128_integer(stored_type) ||
            minic_type_is_long_double(stored_type)) {
            return load_core_value(file, frame, instruction->value.store.address, "t0") &&
                   load_core_int128_value(file, frame, stored_value, "t1", "t2") &&
                   fprintf(file, "  sd t1, 0(t0)\n  sd t2, 8(t0)\n") >= 0;
        }
        return load_core_value(file, frame, instruction->value.store.address, "t0") &&
               load_core_value(file, frame, stored_value, "t1") &&
               minic_riscv64_emit_scalar_store_for_program(file, program, stored_type, "t1", "t0");
    }
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD: {
        const char *opcode;
        size_t destination_offset;
        size_t record_size;

        if (!minic_riscv64_core_record_load_supported(program, function, instruction, &record_size) ||
            !core_object_offset(program,
                                function,
                                frame,
                                instruction->value.record_load.destination_object,
                                &destination_offset) ||
            !load_core_value(
                file, frame, instruction->value.record_load.source_address, "t0")) {
            return false;
        }
        if (record_size <= 8U) {
            opcode = record_size == 8U ? "ld" : record_size == 4U ? "lwu" :
                     record_size == 2U ? "lhu" : "lbu";
            if (fprintf(file, "  %s t1, 0(t0)\n", opcode) < 0 ||
                !emit_frame_store_chunk(file, frame, "t1", destination_offset, record_size)) {
                return false;
            }
            return true;
        }

        /* M162_CORE_RV64_RECORD_LOAD: materialize arbitrary non-empty records
           into the destination CoreObject without assuming source alignment.
           This mirrors RECORD_COPY's byte-safe O0 fallback. */
        if (!emit_frame_address(file, frame, "t1", destination_offset)) {
            return false;
        }
        {
            size_t copied = 0U;
            while (copied < record_size) {
                size_t chunk = record_size - copied;
                size_t offset;
                if (chunk > 2048U) {
                    chunk = 2048U;
                }
                for (offset = 0U; offset < chunk; ++offset) {
                    if (fprintf(file,
                                "  lbu t2, %zu(t0)\n"
                                "  sb t2, %zu(t1)\n",
                                offset,
                                offset) < 0) {
                        return false;
                    }
                }
                copied += chunk;
                if (copied < record_size &&
                    fprintf(file,
                            "  li t3, %zu\n"
                            "  add t0, t0, t3\n"
                            "  add t1, t1, t3\n",
                            chunk) < 0) {
                    return false;
                }
            }
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {
        size_t alignment;
        size_t copied;
        size_t copy_size;

        if (!minic_riscv64_core_record_copy_supported(program, function, instruction) ||
            !minic_data_layout_type(minic_default_data_layout(),
                                    program,
                                    instruction->type,
                                    &copy_size,
                                    &alignment) ||
            !load_core_value(
                file, frame, instruction->value.record_copy.destination_address, "t0") ||
            !load_core_value(file, frame, instruction->value.record_copy.source_address, "t1")) {
            return false;
        }
        (void)alignment;
        copied = 0U;
        while (copied < copy_size) {
            size_t chunk = copy_size - copied;
            size_t offset;
            if (chunk > 2048U) {
                chunk = 2048U;
            }
            for (offset = 0U; offset < chunk; ++offset) {
                if (fprintf(file,
                            "  lbu t2, %zu(t1)\n"
                            "  sb t2, %zu(t0)\n",
                            offset,
                            offset) < 0) {
                    return false;
                }
            }
            copied += chunk;
            if (copied < copy_size &&
                fprintf(file,
                        "  li t3, %zu\n"
                        "  add t0, t0, t3\n"
                        "  add t1, t1, t3\n",
                        chunk) < 0) {
                return false;
            }
        }
        return true;
    }
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
        return emit_opaque_inline_asm(file, function, symbol_name, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
        return emit_register_output_inline_asm(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
        return emit_register_output_input_inline_asm(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
        return emit_memory_readwrite_scalar_input_inline_asm(
            file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return emit_scalar_input_inline_asm(file, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        return emit_structured_inline_asm(file, program, function, frame, instruction, instruction_id);
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return emit_call(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return emit_indirect_call(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return emit_field_address(file, frame, instruction);
    }
    return false;
}

static bool emit_block_label(FILE *file, const char *symbol_name, MinicCoreBlockId block_id) {
    return fprintf(file, ".L%s_core_bb%" PRIu32 ":\n", symbol_name, block_id) >= 0;
}

/* Bootstrap B0 reaches functions large enough that a direct JAL relocation can
   overflow its signed 21-bit displacement. Core values are stack-backed at
   terminators, so t6 is a dead scratch register here. Materialize local CFG
   destinations PC-relatively and jump through t6; this keeps block transfers
   valid across the whole practical function-size range instead of relying on
   assembler/linker JAL relaxation. */
static bool emit_far_jump_to_block(FILE *file,
                                   const char *symbol_name,
                                   MinicCoreBlockId block_id) {
    return file != NULL && symbol_name != NULL &&
           fprintf(file,
                   "  lla t6, .L%s_core_bb%" PRIu32 "\n"
                   "  jalr zero, t6, 0\n",
                   symbol_name,
                   block_id) >= 0;
}

static bool emit_far_jump_to_return(FILE *file, const char *symbol_name) {
    return file != NULL && symbol_name != NULL &&
           fprintf(file,
                   "  lla t6, .L%s_core_return\n"
                   "  jalr zero, t6, 0\n",
                   symbol_name) >= 0;
}

static bool emit_terminator(FILE *file,
                            const MinicC0Program *program,
                            const MinicCoreFunction *function,
                            const MinicRiscv64CoreFrame *frame,
                            const char *symbol_name,
                            const MinicCoreTerminator *terminator) {
    if (file == NULL || function == NULL || frame == NULL || symbol_name == NULL ||
        terminator == NULL) {
        return false;
    }
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (minic_type_is_record(function->return_type)) {
            MinicRiscv64AbiValue return_value;
            size_t object_offset;

            if (program == NULL || terminator->return_object >= function->object_count ||
                !minic_type_equal(function->objects[terminator->return_object].type,
                                  function->return_type) ||
                !minic_riscv64_abi_classify_value(program, function->return_type, &return_value) ||
                !core_object_offset(program, function, frame, terminator->return_object, &object_offset) ||
                !emit_frame_address(file, frame, "t0", object_offset)) {
                return false;
            }
            if (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                if (return_value.slot_count == 0U || return_value.slot_count > 2U ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk(
                        file, program, function->return_type, 0U, "a0", "t0") ||
                    (return_value.slot_count == 2U &&
                     !minic_riscv64_emit_integer_aggregate_load_chunk(
                         file, program, function->return_type, 1U, "a1", "t0"))) {
                    return false;
                }
            } else if (return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                size_t copied;

                if (!frame->has_hidden_result_pointer || return_value.storage_size <= 16U ||
                    return_value.slot_count != 1U ||
                    !emit_frame_load64(
                        file, frame, "t1", frame->hidden_result_pointer_offset)) {
                    return false;
                }
                copied = 0U;
                while (copied < return_value.storage_size) {
                    size_t chunk = return_value.storage_size - copied;
                    size_t offset;

                    if (chunk > 2048U) {
                        chunk = 2048U;
                    }
                    for (offset = 0U; offset < chunk; ++offset) {
                        if (fprintf(file,
                                    "  lbu t2, %zu(t0)\n"
                                    "  sb t2, %zu(t1)\n",
                                    offset,
                                    offset) < 0) {
                            return false;
                        }
                    }
                    copied += chunk;
                    if (copied < return_value.storage_size &&
                        fprintf(file,
                                "  addi t0, t0, 2047\n"
                                "  addi t0, t0, 1\n"
                                "  addi t1, t1, 2047\n"
                                "  addi t1, t1, 1\n") < 0) {
                        return false;
                    }
                }
            } else {
                return false;
            }
        } else if (minic_type_is_long_double(function->return_type)) {
            if (terminator->return_value == MINIC_CORE_VALUE_INVALID ||
                !load_core_int128_value(
                    file, frame, terminator->return_value, "a0", "a1")) {
                return false;
            }
        } else if (minic_type_is_float(function->return_type) ||
                   minic_type_is_double(function->return_type)) {
            const char *move_opcode =
                minic_type_is_double(function->return_type) ? "fmv.d.x" : "fmv.w.x";
            if (terminator->return_value == MINIC_CORE_VALUE_INVALID ||
                !load_core_value(file, frame, terminator->return_value, "a0") ||
                fprintf(file, "  %s fa0, a0\n", move_opcode) < 0) {
                return false;
            }
        } else if (terminator->return_value != MINIC_CORE_VALUE_INVALID &&
                   !load_core_value(file, frame, terminator->return_value, "a0")) {
            return false;
        }
        return emit_far_jump_to_return(file, symbol_name);
    /* M91_BUILTIN_UNREACHABLE_TERMINATOR: reaching this block is UB; no
       target instruction is required. The Core terminator still prevents
       normal CFG fallthrough from being modeled as a supported continuation. */
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return true;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return emit_far_jump_to_block(file, symbol_name, terminator->branch_target);
    /* M158_FINAL_STRICT_TAIL_INDIRECT_BRANCH_RV64 */
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return load_core_value(file, frame, terminator->indirect_target, "t0") &&
               fprintf(file, "  jalr zero, t0, 0\n") >= 0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        /* Keep the architectural conditional branch local, then use the same
           far-transfer sequence for both CFG successors. The numeric local
           label is intentionally reusable; 1f always resolves to the next
           instance emitted by this terminator. */
        return load_core_value(file, frame, terminator->conditional.condition, "t0") &&
               fprintf(file,
                       "  beqz t0, 1f\n"
                       "  lla t6, .L%s_core_bb%" PRIu32 "\n"
                       "  jalr zero, t6, 0\n"
                       "1:\n"
                       "  lla t6, .L%s_core_bb%" PRIu32 "\n"
                       "  jalr zero, t6, 0\n",
                       symbol_name,
                       terminator->conditional.when_true,
                       symbol_name,
                       terminator->conditional.when_false) >= 0;
    }
    return false;
}

/* M177_CONSTANT_SWITCH_CFG_OWNER: user C labels are valid re-entry
   roots for goto/asm-goto/computed-goto, so seed them conservatively alongside
   the function entry and close over explicit Core terminator edges. Synthetic
   switch blocks with no executable predecessor remain unmarked. */
static bool core_frame_mark_reachable_blocks(const MinicCoreFunction *function,
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

static bool emit_core_function_with_symbol(FILE *file,
                                                    const MinicC0Program *program,
                                                    const MinicCoreFunction *function,
                                                    const MinicRiscv64FunctionSymbol *symbol) {
    MinicRiscv64CoreFrame frame;
    const char *symbol_name;
    size_t block_index;
    bool bootstrap_trace;

    if (file == NULL || symbol == NULL || symbol->symbol_name == NULL ||
        symbol->symbol_name[0] == '\0') {
        return false;
    }
    symbol_name = symbol->symbol_name;
    bootstrap_trace = getenv("MINIC_BOOTSTRAP_TRACE") != NULL;
    if (bootstrap_trace) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-capability state=begin "
                      "function=%s blocks=%zu instructions=%zu values=%zu\n",
                      symbol_name,
                      function != NULL ? function->block_count : 0U,
                      function != NULL ? function->instruction_count : 0U,
                      function != NULL ? function->value_count : 0U);
        (void)fflush(stderr);
    }
    if (!core_function_can_emit(program, function)) {
        return false;
    }
    memset(&frame, 0, sizeof(frame));
    minic_riscv64_object_liveness_begin_function();
    if (bootstrap_trace) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-capability state=end "
                      "function=%s\n",
                      symbol_name);
        (void)fflush(stderr);
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-frame state=begin "
                      "function=%s\n",
                      symbol_name);
        (void)fflush(stderr);
    }
    /* Opt-in P1 test changes only the selected function's frame. */
    {
        const char *focus = getenv("MINIC_P1_CROSSBLOCK_FUNCTION");
        minic_riscv64_object_liveness_set_crossblock_enabled(
            focus == NULL || focus[0] == 0 ||
            strcmp(focus, symbol_name) == 0);
    }
    if (!minic_riscv64_asm_function_plan_build(function, &frame.asm_plan) ||
        !core_frame_initialize(program, function, &frame)) {
        return core_frame_fail(&frame);
    }
    if (!core_frame_mark_reachable_blocks(function, &frame)) {
        return core_frame_fail(&frame);
    }
    if (bootstrap_trace) {
        (void)fprintf(stderr,
                      "MINIC_BOOTSTRAP_TRACE stage=core-codegen-frame state=end "
                      "function=%s frame_size=%zu value_slots=%zu values=%zu\n",
                      symbol_name,
                      frame.frame_size,
                      frame.value_slot_count,
                      frame.value_count);
        (void)fflush(stderr);
    }
    if (!minic_riscv64_emit_function_symbol_begin(file, symbol)) {
        return core_frame_fail(&frame);
    }
    if (frame.has_dynamic_stack_alignment) {
        if (fprintf(file, "  mv t0, sp\n") < 0 ||
            !minic_riscv64_emit_stack_allocate(file, frame.frame_size) ||
            fprintf(file,
                    "  li t1, -%zu\n"
                    "  and sp, sp, t1\n",
                    frame.stack_alignment) < 0 ||
            !minic_riscv64_emit_sp_store64(file, "t0", frame.entry_sp_offset)) {
            return core_frame_fail(&frame);
        }
    } else if (!minic_riscv64_emit_stack_allocate(file, frame.frame_size)) {
        return core_frame_fail(&frame);
    }
    if (frame.has_dynamic_stack_alloc) {
        if (!minic_riscv64_emit_sp_store64(
                file, "s0", frame.saved_frame_base_offset) ||
            fprintf(file, "  mv s0, sp\n") < 0 ||
            !minic_riscv64_emit_stack_allocate(file, frame.dynamic_call_area_size)) {
            return core_frame_fail(&frame);
        }
    }
    if (frame.saves_return_address &&
        !emit_frame_store64(file, &frame, "ra", frame.return_address_offset)) {
        return core_frame_fail(&frame);
    }
    if (frame.has_hidden_result_pointer &&
        !emit_frame_store64(file, &frame, "a0", frame.hidden_result_pointer_offset)) {
        return core_frame_fail(&frame);
    }
    if (frame.preserves_structured_asm_callee_saved) {
        size_t saved_index;
        size_t saved_slot = 0U;
        for (saved_index = 0U; saved_index < MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset;
            if ((frame.asm_plan.callee_saved_mask &
                 (uint16_t)(1U << saved_index)) == 0U) {
                continue;
            }
            saved_offset = frame.structured_asm_callee_saved_offset + saved_slot * 8U;
            ++saved_slot;
            if (!emit_frame_store64(
                    file, &frame, minic_riscv64_asm_callee_saved_registers[saved_index], saved_offset)) {
                return core_frame_fail(&frame);
            }
        }
    }
    if (frame.has_variadic_argument_address) {
        size_t register_index;

        for (register_index = frame.integer_parameter_count; register_index < 8U;
             ++register_index) {
            size_t offset = frame.varargs_offset +
                            (register_index - frame.integer_parameter_count) * 8U;
            if (!emit_frame_store64(
                    file, &frame, minic_core_rv64_argument_registers[register_index], offset)) {
                return core_frame_fail(&frame);
            }
        }
    }
    if (!emit_far_jump_to_block(file, symbol_name, function->entry_block)) {
        return core_frame_fail(&frame);
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block;
        size_t instruction_index;

        if (bootstrap_trace &&
            (block_index == 0U || block_index % 128U == 0U ||
             block_index + 1U == function->block_count)) {
            (void)fprintf(stderr,
                          "MINIC_BOOTSTRAP_TRACE stage=core-codegen-block "
                          "function=%s block=%zu total=%zu\n",
                          symbol_name,
                          block_index,
                          function->block_count);
            (void)fflush(stderr);
        }
        block = &function->blocks[block_index];
        if (frame.reachable_blocks != NULL && !frame.reachable_blocks[block_index]) {
            continue;
        }
        if (!emit_block_label(file, symbol_name, (MinicCoreBlockId)block_index)) {
            return core_frame_fail(&frame);
        }
        if (!function->suppress_source_label_aliases &&
            block->source_label_id != SIZE_MAX &&
            fprintf(file, ".Luser_%zu:\n", block->source_label_id) < 0) {
            return core_frame_fail(&frame);
        }
        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            MinicCoreInstructionId instruction_id;

            instruction_id = block->instructions[instruction_index];
            if (instruction_id >= function->instruction_count ||
                !emit_instruction(file, program, function, &frame, symbol_name,
                                  &function->instructions[instruction_id], (size_t)instruction_id)) {
                return core_frame_fail(&frame);
            }
        }
        if (!block->has_terminator ||
            !emit_terminator(file, program, function, &frame, symbol_name, &block->terminator)) {
            return core_frame_fail(&frame);
        }
    }
    if (fprintf(file, ".L%s_core_return:\n", symbol_name) < 0) {
        return core_frame_fail(&frame);
    }
    if (frame.saves_return_address &&
        !emit_frame_load64(file, &frame, "ra", frame.return_address_offset)) {
        return core_frame_fail(&frame);
    }
    if (frame.preserves_structured_asm_callee_saved) {
        size_t saved_index;
        size_t saved_slot = 0U;
        for (saved_index = 0U; saved_index < MINIC_RISCV64_ASM_CALLEE_SAVED_COUNT; ++saved_index) {
            size_t saved_offset;
            if ((frame.asm_plan.callee_saved_mask &
                 (uint16_t)(1U << saved_index)) == 0U) {
                continue;
            }
            saved_offset = frame.structured_asm_callee_saved_offset + saved_slot * 8U;
            ++saved_slot;
            if (!emit_frame_load64(
                    file, &frame, minic_riscv64_asm_callee_saved_registers[saved_index], saved_offset)) {
                return core_frame_fail(&frame);
            }
        }
    }
    if (frame.has_dynamic_stack_alloc) {
        if (!emit_frame_load64(file, &frame, "t4", frame.saved_frame_base_offset)) {
            return core_frame_fail(&frame);
        }
        if (frame.has_dynamic_stack_alignment) {
            if (!emit_frame_load64(file, &frame, "t0", frame.entry_sp_offset)) {
                return core_frame_fail(&frame);
            }
        } else if (frame.frame_size <= 2047U) {
            if (fprintf(file, "  addi t0, s0, %zu\n", frame.frame_size) < 0) {
                return core_frame_fail(&frame);
            }
        } else if (fprintf(file,
                           "  li t0, %zu\n"
                           "  add t0, s0, t0\n",
                           frame.frame_size) < 0) {
            return core_frame_fail(&frame);
        }
        if (fprintf(file,
                    "  mv sp, t0\n"
                    "  mv s0, t4\n") < 0) {
            return core_frame_fail(&frame);
        }
    } else if (frame.has_dynamic_stack_alignment) {
        if (!minic_riscv64_emit_sp_load64(file, "t0", frame.entry_sp_offset) ||
            fprintf(file, "  mv sp, t0\n") < 0) {
            return core_frame_fail(&frame);
        }
    } else if (!minic_riscv64_emit_stack_release(file, frame.frame_size)) {
        return core_frame_fail(&frame);
    }
    if (fprintf(file, "  ret\n") < 0 ||
        !minic_riscv64_emit_function_symbol_end(file, symbol)) {
        return core_frame_fail(&frame);
    }
    core_frame_destroy(&frame);
    return true;
}

bool minic_riscv64_emit_core_function_with_symbol(
    FILE *file, const MinicCoreFunction *function, const MinicRiscv64FunctionSymbol *symbol) {
    return emit_core_function_with_symbol(file, NULL, function, symbol);
}

bool minic_riscv64_emit_core_function_for_program_with_symbol(
    FILE *file,
    const MinicC0Program *program,
    const MinicCoreFunction *function,
    const MinicRiscv64FunctionSymbol *symbol) {
    return program != NULL &&
           emit_core_function_with_symbol(file, program, function, symbol);
}
