#include "target/common/core_spill_layout.h"
#include "core/value_uses.h"
#include "target/data_layout.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* RV64-proven per-block SSA last-use allocator shared by RV32 and RV64.
 * The physical stack geometry comes from the target DataLayout and XLEN. */
/* IRQ_FRAME_REUSE_V0: Core SSA availability is reset at every basic-block
   boundary. Reuse physical spill slots inside one block only after the last
   semantic use of a value. A result is allocated before the current
   instruction's operands are released, so an instruction never aliases an
   output spill with an input that it still has to read. */
/* IRQ_FRAME_PACK_V0: Core scalar spill values are block-local. Keep the
   existing conservative no-liveness-reuse contract, but do not spend 16 bytes
   on every <= XLEN value merely because int128 needs a two-XLEN pair. */
static bool core_value_spill_slot_size(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicDataLayout *layout,
                                       size_t unit_bytes,
                                       MinicCoreValueId value_id,
                                       size_t *cached_slot_sizes,
                                       size_t *slot_size) {
    size_t value_size;
    size_t value_alignment;

    if (program == NULL || function == NULL || slot_size == NULL ||
        cached_slot_sizes == NULL || value_id >= function->value_count)
        return false;
    if (cached_slot_sizes[value_id] != 0U) {
        *slot_size = cached_slot_sizes[value_id];
        return true;
    }
    if (!minic_data_layout_type(layout,
                                program,
                                function->values[value_id].type,
                                &value_size,
                                &value_alignment) ||
        value_size == 0U || value_size > 2U * unit_bytes) {
        return false;
    }
    (void)value_alignment;
    *slot_size = value_size > unit_bytes ? 2U * unit_bytes : unit_bytes;
    cached_slot_sizes[value_id] = *slot_size;
    return true;
}

typedef struct CoreSpillUseCountContext {
    const MinicCoreFunction *function;
    size_t *remaining_uses;
} CoreSpillUseCountContext;

static bool core_spill_count_use(MinicCoreValueId value_id, void *opaque) {
    CoreSpillUseCountContext *context = (CoreSpillUseCountContext *)opaque;
    if (context == NULL || context->function == NULL || context->remaining_uses == NULL ||
        value_id >= context->function->value_count ||
        context->remaining_uses[value_id] == SIZE_MAX) {
        return false;
    }
    ++context->remaining_uses[value_id];
    return true;
}

typedef struct CoreSpillConsumeContext {
    const MinicC0Program *program;
    const MinicCoreFunction *function;
    const MinicDataLayout *layout;
    size_t unit_bytes;
    size_t *cached_slot_sizes;
    size_t *remaining_uses;
    size_t *active_units;
    bool *occupied_units;
    size_t unit_count;
} CoreSpillConsumeContext;

static bool core_spill_consume_use(MinicCoreValueId value_id, void *opaque) {
    CoreSpillConsumeContext *context = (CoreSpillConsumeContext *)opaque;
    size_t slot_size;
    size_t units;
    size_t start;
    size_t i;

    if (context == NULL || context->program == NULL || context->function == NULL ||
        context->remaining_uses == NULL || context->active_units == NULL ||
        context->occupied_units == NULL || value_id >= context->function->value_count ||
        context->remaining_uses[value_id] == 0U ||
        context->active_units[value_id] == SIZE_MAX ||
        !core_value_spill_slot_size(
            context->program, context->function, context->layout,
            context->unit_bytes, value_id, context->cached_slot_sizes,
            &slot_size) ||
        (slot_size != context->unit_bytes &&
         slot_size != 2U * context->unit_bytes)) {
        return false;
    }
    --context->remaining_uses[value_id];
    if (context->remaining_uses[value_id] != 0U) {
        return true;
    }
    units = slot_size / context->unit_bytes;
    start = context->active_units[value_id];
    if (start > context->unit_count || units > context->unit_count - start) {
        return false;
    }
    for (i = 0U; i < units; ++i) {
        if (!context->occupied_units[start + i]) {
            return false;
        }
        context->occupied_units[start + i] = false;
    }
    context->active_units[value_id] = SIZE_MAX;
    return true;
}

static bool core_spill_find_units(const bool *occupied_units,
                                  size_t unit_count,
                                  size_t needed_units,
                                  size_t *start_unit) {
    size_t start;

    if (occupied_units == NULL || start_unit == NULL || needed_units == 0U ||
        needed_units > unit_count) {
        return false;
    }
    for (start = 0U; start <= unit_count - needed_units; ++start) {
        size_t i;
        bool free_run = true;
        for (i = 0U; i < needed_units; ++i) {
            if (occupied_units[start + i]) {
                free_run = false;
                break;
            }
        }
        if (free_run) {
            *start_unit = start;
            return true;
        }
    }
    return false;
}

/* The function workspace caches each immutable value's DataLayout slot size
 * across all SSA reads/releases; do not repeat type-layout calculation.
 * The old algorithm calloc/malloc'd
 * three arrays in every block (and walked all blocks twice); that dominated
 * small-block-heavy functions. Reset buffers in-place between blocks. */
typedef struct CoreSpillWorkspace {
    size_t *cached_slot_sizes;
    size_t *remaining_uses;
    size_t *active_units;
    bool *occupied_units;
    size_t occupied_capacity;
} CoreSpillWorkspace;

static bool core_block_reused_spill_layout(const MinicC0Program *program,
                                           const MinicCoreFunction *function,
                                           const MinicCoreBlock *block,
                                           const MinicDataLayout *layout,
                                           size_t unit_bytes,
                                           size_t value_base_offset,
                                           size_t *value_offsets,
                                           size_t *peak_bytes,
                                           CoreSpillWorkspace *workspace) {
    size_t *remaining_uses = workspace != NULL ? workspace->remaining_uses : NULL;
    size_t *active_units = workspace != NULL ? workspace->active_units : NULL;
    bool *occupied_units = workspace != NULL ? workspace->occupied_units : NULL;
    size_t total_units = 0U;
    size_t peak_units = 0U;
    size_t instruction_index;
    CoreSpillUseCountContext count_context;
    CoreSpillConsumeContext consume_context;
    bool ok = false;

    if (program == NULL || function == NULL || block == NULL || layout == NULL ||
        peak_bytes == NULL || workspace == NULL ||
        (unit_bytes != 4U && unit_bytes != 8U) ||
        value_base_offset % unit_bytes != 0U) {
        return false;
    }
    for (instruction_index = 0U; instruction_index < block->instruction_count;
         ++instruction_index) {
        MinicCoreInstructionId instruction_id = block->instructions[instruction_index];
        const MinicCoreInstruction *instruction;
        size_t slot_size;
        if (instruction_id >= function->instruction_count) {
            return false;
        }
        instruction = &function->instructions[instruction_id];
        if (instruction->result == MINIC_CORE_VALUE_INVALID) {
            continue;
        }
        if (instruction->result >= function->value_count ||
            !core_value_spill_slot_size(program, function, layout, unit_bytes,
                                        instruction->result,
                                        workspace->cached_slot_sizes, &slot_size) ||
            (slot_size != unit_bytes && slot_size != 2U * unit_bytes) ||
            total_units > SIZE_MAX - slot_size / unit_bytes) {
            return false;
        }
        total_units += slot_size / unit_bytes;
    }
    if (total_units == 0U) {
        *peak_bytes = 0U;
        return true;
    }
    if (remaining_uses == NULL || active_units == NULL)
        return false;
    if (total_units > workspace->occupied_capacity) {
        bool *expanded = (bool *)realloc(workspace->occupied_units,
                                        total_units * sizeof(*expanded));
        if (expanded == NULL) return false;
        workspace->occupied_units = expanded;
        workspace->occupied_capacity = total_units;
    }
    occupied_units = workspace->occupied_units;
    (void)memset(remaining_uses, 0, function->value_count * sizeof(*remaining_uses));
    (void)memset(occupied_units, 0, total_units * sizeof(*occupied_units));
    for (instruction_index = 0U; instruction_index < function->value_count;
         ++instruction_index) {
        active_units[instruction_index] = SIZE_MAX;
    }
    count_context.function = function;
    count_context.remaining_uses = remaining_uses;
    for (instruction_index = 0U; instruction_index < block->instruction_count;
         ++instruction_index) {
        MinicCoreInstructionId instruction_id = block->instructions[instruction_index];
        if (instruction_id >= function->instruction_count ||
            !minic_core_visit_instruction_value_uses(function,
                                               &function->instructions[instruction_id],
                                               core_spill_count_use,
                                               &count_context)) {
            goto done;
        }
    }
    if (!block->has_terminator ||
        !minic_core_visit_terminator_value_uses(
            &block->terminator, core_spill_count_use, &count_context)) {
        goto done;
    }

    consume_context.program = program;
    consume_context.function = function;
    consume_context.layout = layout;
    consume_context.unit_bytes = unit_bytes;
    consume_context.cached_slot_sizes = workspace->cached_slot_sizes;
    consume_context.remaining_uses = remaining_uses;
    consume_context.active_units = active_units;
    consume_context.occupied_units = occupied_units;
    consume_context.unit_count = total_units;

    for (instruction_index = 0U; instruction_index < block->instruction_count;
         ++instruction_index) {
        MinicCoreInstructionId instruction_id = block->instructions[instruction_index];
        const MinicCoreInstruction *instruction = &function->instructions[instruction_id];

        if (instruction->result != MINIC_CORE_VALUE_INVALID) {
            size_t slot_size;
            size_t units;
            size_t start;
            size_t i;
            MinicCoreValueId result = instruction->result;

            if (result >= function->value_count || active_units[result] != SIZE_MAX ||
                !core_value_spill_slot_size(program, function, layout, unit_bytes,
                                            result, workspace->cached_slot_sizes,
                                            &slot_size) ||
                (slot_size != unit_bytes && slot_size != 2U * unit_bytes)) {
                goto done;
            }
            units = slot_size / unit_bytes;
            if (!core_spill_find_units(occupied_units, total_units, units, &start)) {
                goto done;
            }
            for (i = 0U; i < units; ++i) {
                occupied_units[start + i] = true;
            }
            active_units[result] = start;
            if (start + units > peak_units) {
                peak_units = start + units;
            }
            if (value_offsets != NULL) {
                if (value_offsets[result] != SIZE_MAX || start > SIZE_MAX / unit_bytes ||
                    value_base_offset > SIZE_MAX - start * unit_bytes) {
                    goto done;
                }
                value_offsets[result] = value_base_offset + start * unit_bytes;
            }
        }

        if (!minic_core_visit_instruction_value_uses(function,
                                               instruction,
                                               core_spill_consume_use,
                                               &consume_context)) {
            goto done;
        }
        if (instruction->result != MINIC_CORE_VALUE_INVALID &&
            remaining_uses[instruction->result] == 0U) {
            MinicCoreValueId result = instruction->result;
            size_t slot_size;
            size_t units;
            size_t start = active_units[result];
            size_t i;
            if (start == SIZE_MAX ||
                !core_value_spill_slot_size(program, function, layout, unit_bytes,
                                            result, workspace->cached_slot_sizes,
                                            &slot_size) ||
                (slot_size != unit_bytes && slot_size != 2U * unit_bytes)) {
                goto done;
            }
            units = slot_size / unit_bytes;
            if (start > total_units || units > total_units - start) {
                goto done;
            }
            for (i = 0U; i < units; ++i) {
                if (!occupied_units[start + i]) {
                    goto done;
                }
                occupied_units[start + i] = false;
            }
            active_units[result] = SIZE_MAX;
        }
    }
    if (!minic_core_visit_terminator_value_uses(
            &block->terminator, core_spill_consume_use, &consume_context)) {
        goto done;
    }
    *peak_bytes = peak_units * unit_bytes;
    ok = true;

done:
    return ok;
}


/* One function-wide SSA spill plan: each Core block is scanned once and
 * a reusable scratch workspace amortizes all per-block allocations. RV32I
 * and RV64 retain identical last-use rules, 4B/8B units, placement order
 * and ABI-chosen stack base. The only public spill API is this one. */
bool minic_core_function_reused_spill_layout(
    const MinicC0Program *program, const MinicCoreFunction *function,
    const MinicDataLayout *layout, size_t unit_bytes,
    size_t value_base_offset, size_t *value_offsets,
    size_t *peak_bytes, size_t *failed_block) {
    CoreSpillWorkspace workspace = {0};
    size_t block_index, maximum = 0U;
    bool ok = false;
    if (failed_block != NULL) *failed_block = SIZE_MAX;
    if (program == NULL || function == NULL || layout == NULL ||
        peak_bytes == NULL || (unit_bytes != 4U && unit_bytes != 8U) ||
        value_base_offset % unit_bytes != 0U ||
        (function->value_count != 0U && value_offsets == NULL) ||
        (function->block_count != 0U && function->blocks == NULL) ||
        function->value_count > SIZE_MAX / sizeof(size_t))
        return false;

    if (function->value_count != 0U && function->block_count != 0U) {
        workspace.cached_slot_sizes =
            (size_t *)calloc(function->value_count,
                             sizeof(*workspace.cached_slot_sizes));
        workspace.remaining_uses =
            (size_t *)calloc(function->value_count,
                             sizeof(*workspace.remaining_uses));
        workspace.active_units =
            (size_t *)malloc(function->value_count *
                             sizeof(*workspace.active_units));
        if (workspace.cached_slot_sizes == NULL ||
            workspace.remaining_uses == NULL || workspace.active_units == NULL)
            goto done;
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        size_t block_bytes;
        if (!core_block_reused_spill_layout(
                program, function, &function->blocks[block_index],
                layout, unit_bytes, value_base_offset, value_offsets,
                &block_bytes, &workspace)) {
            if (failed_block != NULL) *failed_block = block_index;
            goto done;
        }
        if (block_bytes > maximum) maximum = block_bytes;
    }
    if (maximum % unit_bytes != 0U ||
        maximum > SIZE_MAX - value_base_offset)
        goto done;
    *peak_bytes = maximum;
    ok = true;
done:
    free(workspace.cached_slot_sizes);
    free(workspace.remaining_uses);
    free(workspace.active_units);
    free(workspace.occupied_units);
    return ok;
}
