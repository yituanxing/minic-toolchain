#!/usr/bin/env python3
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
text = p.read_text()

if "IRQ_FRAME_PACK_V0" not in text:
    raise SystemExit("packed spill-slot prerequisite not applied")
if "IRQ_FRAME_REUSE_V0" in text:
    raise SystemExit("block-local spill-slot reuse already applied")

frame_marker = "static bool core_frame_initialize(const MinicC0Program *program,"
if frame_marker not in text:
    raise SystemExit("core_frame_initialize marker not found")

helper = r'''/* IRQ_FRAME_REUSE_V0: Core SSA availability is reset at every basic-block
   boundary. Reuse physical spill slots inside one block only after the last
   semantic use of a value. A result is allocated before the current
   instruction's operands are released, so an instruction never aliases an
   output spill with an input that it still has to read. */
typedef bool (*CoreValueUseVisitor)(MinicCoreValueId value_id, void *context);

static bool core_visit_value_use(MinicCoreValueId value_id,
                                 CoreValueUseVisitor visitor,
                                 void *context) {
    return visitor != NULL && value_id != MINIC_CORE_VALUE_INVALID &&
           visitor(value_id, context);
}

static bool core_visit_call_argument_uses(const MinicCoreFunction *function,
                                          size_t argument_begin,
                                          size_t argument_count,
                                          CoreValueUseVisitor visitor,
                                          void *context) {
    size_t i;

    if (function == NULL || argument_begin > function->call_argument_count ||
        argument_count > function->call_argument_count - argument_begin) {
        return false;
    }
    for (i = 0U; i < argument_count; ++i) {
        const MinicCoreCallArgument *argument =
            &function->call_arguments[argument_begin + i];
        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE &&
            !core_visit_value_use(argument->value.value_id, visitor, context)) {
            return false;
        }
    }
    return true;
}

static bool core_visit_instruction_value_uses(const MinicCoreFunction *function,
                                              const MinicCoreInstruction *instruction,
                                              CoreValueUseVisitor visitor,
                                              void *context) {
    size_t i;

    if (function == NULL || instruction == NULL || visitor == NULL) {
        return false;
    }
    switch (instruction->kind) {
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
        return core_visit_value_use(instruction->value.binary.left, visitor, context) &&
               core_visit_value_use(instruction->value.binary.right, visitor, context);

    case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW:
        return core_visit_value_use(instruction->value.integer_overflow.left, visitor, context) &&
               core_visit_value_use(instruction->value.integer_overflow.right, visitor, context) &&
               core_visit_value_use(
                   instruction->value.integer_overflow.result_address, visitor, context);

    case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION:
    case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER:
    case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST:
    case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE:
    case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT:
    case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
    case MINIC_CORE_INSTRUCTION_INTEGER_CTZ:
    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
    case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE:
    case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE:
    case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT:
    case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC:
        return core_visit_value_use(instruction->value.operand, visitor, context);

    case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS:
        return core_visit_value_use(instruction->value.field_address.base, visitor, context);
    case MINIC_CORE_INSTRUCTION_POINTER_OFFSET:
        return core_visit_value_use(instruction->value.pointer_offset.base, visitor, context) &&
               core_visit_value_use(instruction->value.pointer_offset.index, visitor, context);
    case MINIC_CORE_INSTRUCTION_LOAD:
        return core_visit_value_use(instruction->value.load.address, visitor, context);
    case MINIC_CORE_INSTRUCTION_STORE:
        return core_visit_value_use(instruction->value.store.address, visitor, context) &&
               core_visit_value_use(instruction->value.store.stored_value, visitor, context);
    case MINIC_CORE_INSTRUCTION_RECORD_LOAD:
        return core_visit_value_use(
            instruction->value.record_load.source_address, visitor, context);
    case MINIC_CORE_INSTRUCTION_RECORD_COPY:
        return core_visit_value_use(
                   instruction->value.record_copy.destination_address, visitor, context) &&
               core_visit_value_use(
                   instruction->value.record_copy.source_address, visitor, context);
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INPUT_INLINE_ASM:
        return core_visit_value_use(
            instruction->value.register_output_input_inline_asm.operand, visitor, context);
    case MINIC_CORE_INSTRUCTION_MEMORY_READWRITE_SCALAR_INPUT_INLINE_ASM:
        return core_visit_value_use(
                   instruction->value.memory_readwrite_scalar_input_inline_asm.memory_address,
                   visitor,
                   context) &&
               core_visit_value_use(
                   instruction->value.memory_readwrite_scalar_input_inline_asm.operand,
                   visitor,
                   context);
    case MINIC_CORE_INSTRUCTION_SCALAR_INPUT_INLINE_ASM:
        return core_visit_value_use(
            instruction->value.scalar_input_inline_asm.operand, visitor, context);
    case MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM:
        if (instruction->value.structured_inline_asm.operand_count >
            MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT) {
            return false;
        }
        for (i = 0U; i < instruction->value.structured_inline_asm.operand_count; ++i) {
            if (!core_visit_value_use(
                    instruction->value.structured_inline_asm.operands[i].value,
                    visitor,
                    context)) {
                return false;
            }
        }
        return true;
    case MINIC_CORE_INSTRUCTION_CALL:
        return core_visit_call_argument_uses(function,
                                             instruction->value.call.argument_begin,
                                             instruction->value.call.argument_count,
                                             visitor,
                                             context);
    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:
        return core_visit_value_use(instruction->value.indirect_call.callee, visitor, context) &&
               core_visit_call_argument_uses(function,
                                             instruction->value.indirect_call.argument_begin,
                                             instruction->value.indirect_call.argument_count,
                                             visitor,
                                             context);

    case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT:
    case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_FIXED_REGISTER_READ:
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
    case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
    case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS:
    case MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS:
    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_REGISTER_OUTPUT_INLINE_ASM:
    case MINIC_CORE_INSTRUCTION_COMPILER_BARRIER:
    case MINIC_CORE_INSTRUCTION_CALL_FRAME_ADDRESS:
    case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS:
        return true;
    }
    return false;
}

static bool core_visit_terminator_value_uses(const MinicCoreTerminator *terminator,
                                             CoreValueUseVisitor visitor,
                                             void *context) {
    if (terminator == NULL || visitor == NULL) {
        return false;
    }
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        return terminator->return_value == MINIC_CORE_VALUE_INVALID ||
               core_visit_value_use(terminator->return_value, visitor, context);
    case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
        return core_visit_value_use(terminator->indirect_target, visitor, context);
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        return core_visit_value_use(terminator->conditional.condition, visitor, context);
    case MINIC_CORE_TERMINATOR_BRANCH:
    case MINIC_CORE_TERMINATOR_UNREACHABLE:
        return true;
    }
    return false;
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

static bool core_block_reused_spill_layout(const MinicC0Program *program,
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
            !core_visit_instruction_value_uses(function,
                                               &function->instructions[instruction_id],
                                               core_spill_count_use,
                                               &count_context)) {
            goto done;
        }
    }
    if (!block->has_terminator ||
        !core_visit_terminator_value_uses(
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

        if (!core_visit_instruction_value_uses(function,
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
    if (!core_visit_terminator_value_uses(
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

'''
text = text.replace(frame_marker, helper + frame_marker, 1)

sizing_start_marker = "    /*\n    ** Core values remain block-local and values from distinct blocks reuse the"
sizing_end_marker = "    frame->saves_return_address = core_function_needs_saved_return_address(function);"
sizing_start = text.find(sizing_start_marker)
sizing_end = text.find(sizing_end_marker, sizing_start)
if sizing_start < 0 or sizing_end < 0:
    raise SystemExit("packed sizing block not found")
new_sizing = r'''    /* Core block-local last-use reuse: compute the maximum physical spill
       extent needed by any one block. Distinct blocks still share the region. */
    {
        size_t block_index;
        size_t maximum_block_bytes = 0U;

        for (block_index = 0U; block_index < function->block_count; ++block_index) {
            size_t block_bytes;
            if (!core_block_reused_spill_layout(program,
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
'''
text = text[:sizing_start] + new_sizing + text[sizing_end:]

alloc_anchor = "        for (value_index = 0U; value_index < function->value_count; ++value_index) {\n            frame->value_offsets[value_index] = SIZE_MAX;\n        }\n\n"
anchor_pos = text.find(alloc_anchor)
if anchor_pos < 0:
    raise SystemExit("value offset initialization anchor not found")
assignment_start_marker = "        for (block_index = 0U; block_index < function->block_count; ++block_index) {"
assignment_start = text.find(assignment_start_marker, anchor_pos + len(alloc_anchor))
assignment_end_marker = "        for (value_index = 0U; value_index < function->value_count; ++value_index) {\n            if (frame->value_offsets[value_index] == SIZE_MAX) {"
assignment_end = text.find(assignment_end_marker, assignment_start)
if assignment_start < 0 or assignment_end < 0:
    raise SystemExit("packed value assignment block not found")
new_assignment = r'''        for (block_index = 0U; block_index < function->block_count; ++block_index) {
            size_t block_bytes;
            if (!core_block_reused_spill_layout(program,
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
'''
text = text[:assignment_start] + new_assignment + text[assignment_end:]

trace_marker = '"CORE_FRAME function=%s frame=%zu value_bytes=%zu values=%zu objects=%zu\\n",'
if trace_marker in text:
    text = text.replace(trace_marker,
                        '"CORE_FRAME_REUSE function=%s frame=%zu value_bytes=%zu values=%zu objects=%zu\\n",',
                        1)

p.write_text(text)
print("MINIC_RISCV64_CORE_VALUE_SLOT_REUSE_V0=APPLIED")
