#include "target/riscv64/spill_layout.h"
#include "core/value_uses.h"
#include "target/data_layout.h"
#include <stdint.h>
#include <stdlib.h>
/* The optimized RV64 block-local 8/16-byte spill algorithm remains unchanged. */
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
                                       MinicCoreValueId value_id,
                                       size_t *slot_size) {
    size_t value_size;
    size_t value_alignment;

    if (program == NULL || function == NULL || slot_size == NULL ||
        value_id >= function->value_count ||
        !minic_data_layout_type(minic_default_data_layout(),
                                program,
                                function->values[value_id].type,
                                &value_size,
                                &value_alignment) ||
        value_size == 0U || value_size > 16U) {
        return false;
    }
    (void)value_alignment;
    *slot_size = value_size > 8U ? 16U : 8U;
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
            context->program, context->function, value_id, &slot_size) ||
        (slot_size != 8U && slot_size != 16U)) {
        return false;
    }
    --context->remaining_uses[value_id];
    if (context->remaining_uses[value_id] != 0U) {
        return true;
    }
    units = slot_size / 8U;
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

bool minic_riscv64_block_reused_spill_layout(const MinicC0Program *program,
                                           const MinicCoreFunction *function,
                                           const MinicCoreBlock *block,
                                           size_t value_base_offset,
                                           size_t *value_offsets,
                                           size_t *peak_bytes) {
    size_t *remaining_uses = NULL;
    size_t *active_units = NULL;
    bool *occupied_units = NULL;
    size_t total_units = 0U;
    size_t peak_units = 0U;
    size_t instruction_index;
    CoreSpillUseCountContext count_context;
    CoreSpillConsumeContext consume_context;
    bool ok = false;

    if (program == NULL || function == NULL || block == NULL || peak_bytes == NULL) {
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
            !core_value_spill_slot_size(program, function, instruction->result, &slot_size) ||
            (slot_size != 8U && slot_size != 16U) ||
            total_units > SIZE_MAX - slot_size / 8U) {
            return false;
        }
        total_units += slot_size / 8U;
    }
    if (total_units == 0U) {
        *peak_bytes = 0U;
        return true;
    }
    if (function->value_count > SIZE_MAX / sizeof(*remaining_uses) ||
        function->value_count > SIZE_MAX / sizeof(*active_units)) {
        return false;
    }
    remaining_uses = (size_t *)calloc(function->value_count, sizeof(*remaining_uses));
    active_units = (size_t *)malloc(function->value_count * sizeof(*active_units));
    occupied_units = (bool *)calloc(total_units, sizeof(*occupied_units));
    if (remaining_uses == NULL || active_units == NULL || occupied_units == NULL) {
        goto done;
    }
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
                !core_value_spill_slot_size(program, function, result, &slot_size) ||
                (slot_size != 8U && slot_size != 16U)) {
                goto done;
            }
            units = slot_size / 8U;
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
                if (value_offsets[result] != SIZE_MAX || start > SIZE_MAX / 8U ||
                    value_base_offset > SIZE_MAX - start * 8U) {
                    goto done;
                }
                value_offsets[result] = value_base_offset + start * 8U;
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
                !core_value_spill_slot_size(program, function, result, &slot_size) ||
                (slot_size != 8U && slot_size != 16U)) {
                goto done;
            }
            units = slot_size / 8U;
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
    *peak_bytes = peak_units * 8U;
    ok = true;

done:
    free(occupied_units);
    free(active_units);
    free(remaining_uses);
    return ok;
}

