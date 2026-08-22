#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M30 {label} anchor count={count}, expected 1")
    return text.replace(old, new, 1)


def replace_function(text: str, start_marker: str, end_marker: str, replacement: str, label: str) -> str:
    if text.count(start_marker) != 1 or text.count(end_marker) < 1:
        raise SystemExit(f"M30 {label} function markers not unique/present")
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    return text[:start] + replacement + text[end:]


# ---------------------------------------------------------------------------
# Core IR: model aggregate formal ingress as an effect on an addressable object,
# and aggregate return as returning an object rather than pretending records are
# scalar SSA/CoreValue values.
# ---------------------------------------------------------------------------
h = Path('src/core/core_ir.h')
text = h.read_text()
text = replace_once(
    text,
    '    MINIC_CORE_INSTRUCTION_PARAMETER,\n    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,\n',
    '    MINIC_CORE_INSTRUCTION_PARAMETER,\n    MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT,\n    MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS,\n',
    'core-ir instruction enum',
)
text = replace_once(
    text,
    '        size_t parameter_index;\n        MinicCoreObjectId object_id;\n',
    '''        size_t parameter_index;
        struct {
            size_t parameter_index;
            MinicCoreObjectId object_id;
        } parameter_object;
        MinicCoreObjectId object_id;
''',
    'core-ir parameter object payload',
)
text = replace_once(
    text,
    '    MinicCoreValueId return_value;\n    MinicCoreBlockId branch_target;\n',
    '    MinicCoreValueId return_value;\n    MinicCoreObjectId return_object;\n    MinicCoreBlockId branch_target;\n',
    'core-ir return object',
)
h.write_text(text)

c = Path('src/core/core_ir.c')
text = c.read_text()
text = replace_once(
    text,
    '''    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return instruction_result_is_valid(function, instruction) &&
               instruction->value.parameter_index < function->parameter_count &&
               minic_type_equal(function->parameter_types[instruction->value.parameter_index],
                                instruction->type);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
''',
    '''    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return instruction_result_is_valid(function, instruction) &&
               instruction->value.parameter_index < function->parameter_count &&
               minic_type_equal(function->parameter_types[instruction->value.parameter_index],
                                instruction->type);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_void(instruction->type) &&
               instruction->value.parameter_object.parameter_index < function->parameter_count &&
               instruction->value.parameter_object.object_id < function->object_count &&
               minic_type_is_record(
                   function->parameter_types[instruction->value.parameter_object.parameter_index]) &&
               minic_type_equal(
                   function->parameter_types[instruction->value.parameter_object.parameter_index],
                   function->objects[instruction->value.parameter_object.object_id].type);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
''',
    'core-ir parameter object verify',
)
text = replace_once(
    text,
    '''    case MINIC_CORE_TERMINATOR_RETURN:
        if (minic_type_is_void(function->return_type)) {
            return terminator->return_value == MINIC_CORE_VALUE_INVALID;
        }
        return terminator->return_value < function->value_count &&
               available_values[terminator->return_value] &&
               minic_type_equal(function->values[terminator->return_value].type,
                                function->return_type);
''',
    '''    case MINIC_CORE_TERMINATOR_RETURN:
        if (minic_type_is_void(function->return_type)) {
            return terminator->return_value == MINIC_CORE_VALUE_INVALID;
        }
        if (minic_type_is_record(function->return_type)) {
            return terminator->return_value == MINIC_CORE_VALUE_INVALID &&
                   terminator->return_object < function->object_count &&
                   minic_type_equal(function->objects[terminator->return_object].type,
                                    function->return_type);
        }
        return terminator->return_value < function->value_count &&
               available_values[terminator->return_value] &&
               minic_type_equal(function->values[terminator->return_value].type,
                                function->return_type);
''',
    'core-ir return object verify',
)
text = replace_once(
    text,
    '''    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return fprintf(output,
                       "  %%%" PRIu32 " = parameter %zu\\n",
                       instruction->result,
                       instruction->value.parameter_index) >= 0;
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
''',
    '''    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return fprintf(output,
                       "  %%%" PRIu32 " = parameter %zu\\n",
                       instruction->result,
                       instruction->value.parameter_index) >= 0;
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return fprintf(output,
                       "  parameter.object %zu, %%o%" PRIu32 "\\n",
                       instruction->value.parameter_object.parameter_index,
                       instruction->value.parameter_object.object_id) >= 0;
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
''',
    'core-ir parameter object dump',
)
text = replace_once(
    text,
    '''static bool dump_terminator(FILE *output, const MinicCoreTerminator *terminator) {
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (terminator->return_value == MINIC_CORE_VALUE_INVALID) {
            return fprintf(output, "  return\\n") >= 0;
        }
        return fprintf(output, "  return %%%" PRIu32 "\\n", terminator->return_value) >= 0;
''',
    '''static bool dump_terminator(FILE *output,
                            const MinicCoreFunction *function,
                            const MinicCoreTerminator *terminator) {
    switch (terminator->kind) {
    case MINIC_CORE_TERMINATOR_RETURN:
        if (function != NULL && minic_type_is_record(function->return_type)) {
            return fprintf(output, "  return.object %%o%" PRIu32 "\\n", terminator->return_object) >=
                   0;
        }
        if (terminator->return_value == MINIC_CORE_VALUE_INVALID) {
            return fprintf(output, "  return\\n") >= 0;
        }
        return fprintf(output, "  return %%%" PRIu32 "\\n", terminator->return_value) >= 0;
''',
    'core-ir terminator dump signature',
)
text = replace_once(
    text,
    '        if (!dump_terminator(output, &block->terminator)) {\n',
    '        if (!dump_terminator(output, function, &block->terminator)) {\n',
    'core-ir terminator dump call',
)
c.write_text(text)

# ---------------------------------------------------------------------------
# Core lowering: record parameters become addressable objects at function entry;
# returning a local/by-value parameter record returns its Core object.
# ---------------------------------------------------------------------------
p = Path('src/core/core_lower.c')
text = p.read_text()
new_parameter_ingress = r'''static MinicCoreLowerStatus lower_parameter_ingress(MinicCoreLowerContext *context) {
    size_t parameter_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->source_function == NULL || context->function == NULL ||
        context->source_function->parameter_count > context->source_function->local_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (parameter_index = 0U; parameter_index < context->source_function->parameter_count;
         ++parameter_index) {
        const MinicLocal *parameter;
        MinicCoreInstruction instruction;
        MinicCoreObjectId object_id;
        MinicCoreLowerStatus status;
        MinicLocalId local_id;
        MinicType parameter_value_type;

        local_id = context->source_function->local_begin + parameter_index;
        parameter = minic_c0_program_local(context->body->program, local_id);
        if (parameter == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_volatile(parameter->type) || parameter->is_array ||
            parameter->is_register_storage ||
            !minic_type_unqualified(parameter->type, &parameter_value_type) ||
            !minic_type_equal(parameter_value_type,
                              context->source_function->parameter_types[parameter_index])) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_local_object(context, local_id, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        if (minic_type_is_record(parameter_value_type)) {
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT;
            instruction.span = parameter->name_span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.parameter_object.parameter_index = parameter_index;
            instruction.value.parameter_object.object_id = object_id;
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &instruction)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            continue;
        }
        if (!core_memory_scalar_type(parameter_value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        {
            MinicCoreValueId address_id;
            MinicCoreValueId parameter_value;
            MinicType pointer_type;

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_PARAMETER;
            instruction.span = parameter->name_span;
            instruction.type = parameter_value_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.parameter_index = parameter_index;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &parameter_value)) {
                return MINIC_CORE_LOWER_ERROR;
            }

            if (!minic_type_pointer_to(parameter->type, &pointer_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
            instruction.span = parameter->name_span;
            instruction.type = pointer_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.object_id = object_id;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &address_id)) {
                return MINIC_CORE_LOWER_ERROR;
            }

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
            instruction.span = parameter->name_span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.store.address = address_id;
            instruction.value.store.stored_value = parameter_value;
            instruction.value.store.is_volatile = false;
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &instruction)) {
                return MINIC_CORE_LOWER_ERROR;
            }
        }
    }
    return MINIC_CORE_LOWER_OK;
}

'''
text = replace_function(text,
                        'static MinicCoreLowerStatus lower_parameter_ingress',
                        'static MinicCoreLowerStatus append_field_address',
                        new_parameter_ingress,
                        'parameter ingress')

new_lower_return = r'''static MinicCoreLowerStatus lower_return(MinicCoreLowerContext *context,
                                         const MinicStatement *statement) {
    MinicCoreTerminator terminator;
    MinicCoreLowerStatus status;

    if (context == NULL || context->source_function == NULL || statement == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_RETURN;
    terminator.span = statement->span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.return_object = MINIC_CORE_OBJECT_INVALID;
    if (minic_type_is_void(context->source_function->return_type)) {
        if (statement->expression != MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    } else {
        if (statement->expression == MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_integer(context->source_function->return_type)) {
            status = lower_integer_assignment_value(context,
                                                    context->source_function->return_type,
                                                    statement->expression,
                                                    &terminator.return_value);
        } else if (minic_type_is_pointer(context->source_function->return_type)) {
            status = lower_expression(context, statement->expression, &terminator.return_value);
            if (status == MINIC_CORE_LOWER_OK &&
                (terminator.return_value >= context->function->value_count ||
                 !minic_type_equal(context->function->values[terminator.return_value].type,
                                   context->source_function->return_type))) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        } else if (minic_type_is_record(context->source_function->return_type)) {
            const MinicExpression *expression;
            const MinicLocal *local;
            MinicType value_type;

            expression = minic_c0_program_expression(context->body->program, statement->expression);
            if (expression == NULL || expression->kind != MINIC_EXPRESSION_LOCAL ||
                expression->value_category != MINIC_VALUE_LVALUE ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !minic_type_equal(value_type, context->source_function->return_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            local = minic_c0_program_local(context->body->program, expression->value.local_id);
            if (local == NULL || !minic_type_is_record(local->type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = lower_local_object(
                context, expression->value.local_id, &terminator.return_object);
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    return minic_core_function_set_terminator(context->function, context->block_id, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

'''
text = replace_function(text,
                        'static MinicCoreLowerStatus lower_return',
                        'static MinicCoreLowerStatus set_branch',
                        new_lower_return,
                        'return lowering')
p.write_text(text)

# ---------------------------------------------------------------------------
# RV64 Core emitter: consume the already-canonical RV64 ABI placement owner for
# both scalar and aggregate formal ingress, and use the existing aggregate chunk
# loader for small record returns. Core itself stays register-agnostic.
# ---------------------------------------------------------------------------
p = Path('src/target/riscv64/core_codegen.c')
text = p.read_text()
text = replace_once(
    text,
    '#include "target/riscv64/codegen_internal.h"\n#include "target/data_layout.h"\n',
    '#include "target/riscv64/codegen_internal.h"\n#include "target/riscv64/abi.h"\n#include "target/data_layout.h"\n',
    'core-codegen ABI include',
)
text = replace_once(
    text,
    '''    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
''',
    '''    case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO:
    case MINIC_CORE_INSTRUCTION_PARAMETER:
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
''',
    'core-codegen instruction support',
)

old_signature = '''    if (function == NULL || !minic_core_function_verify(function) ||
        (!minic_type_is_void(function->return_type) && !core_scalar_type(function->return_type))) {
        return false;
    }
    for (index = 0U; index < function->parameter_count; ++index) {
        if (!core_scalar_type(function->parameter_types[index])) {
            return false;
        }
    }
'''
new_signature = '''    if (function == NULL || !minic_core_function_verify(function)) {
        return false;
    }
    if (program == NULL) {
        if (!minic_type_is_void(function->return_type) && !core_scalar_type(function->return_type)) {
            return false;
        }
        for (index = 0U; index < function->parameter_count; ++index) {
            if (!core_scalar_type(function->parameter_types[index])) {
                return false;
            }
        }
    } else {
        MinicRiscv64AbiCursor cursor;
        MinicRiscv64AbiValue return_value;

        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, function->return_type, &cursor, &return_value) ||
            (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
             return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
             (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
              return_value.slot_count == 0U || return_value.slot_count > 2U))) {
            return false;
        }
        for (index = 0U; index < function->parameter_count; ++index) {
            MinicRiscv64AbiArgumentLocation location;

            if (!minic_riscv64_abi_place_argument(
                    program, function->parameter_types[index], true, &cursor, &location) ||
                (location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                 (location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                  location.value.slot_count == 0U || location.value.slot_count > 2U))) {
                return false;
            }
        }
    }
'''
text = replace_once(text, old_signature, new_signature, 'core-codegen signature ABI support')

emit_parameter_start = 'static bool emit_parameter(FILE *file,'
emit_call_marker = 'static bool emit_call(FILE *file,'
new_parameter_helpers = r'''static bool core_parameter_location(const MinicC0Program *program,
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

static bool emit_parameter(FILE *file,
                           const MinicC0Program *program,
                           const MinicCoreFunction *function,
                           const MinicRiscv64CoreFrame *frame,
                           const MinicCoreInstruction *instruction) {
    size_t parameter_index;
    size_t incoming_offset;

    parameter_index = instruction->value.parameter_index;
    if (parameter_index >= function->parameter_count) {
        return false;
    }
    if (program != NULL) {
        MinicRiscv64AbiArgumentLocation location;

        if (!core_parameter_location(program, function, parameter_index, &location) ||
            location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
            location.floating_register_count != 0U) {
            return false;
        }
        if (location.integer_register_count == 1U && location.stack_slot_count == 0U &&
            location.integer_register_begin < 8U) {
            if (fprintf(file,
                        "  mv t0, %s\n",
                        minic_core_rv64_argument_registers[location.integer_register_begin]) < 0) {
                return false;
            }
        } else if (location.integer_register_count == 0U && location.stack_slot_count == 1U) {
            if (location.stack_slot_begin > (SIZE_MAX - frame->frame_size) / 8U) {
                return false;
            }
            incoming_offset = frame->frame_size + location.stack_slot_begin * 8U;
            if (!minic_riscv64_emit_sp_load64(file, "t0", incoming_offset)) {
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
        if (stack_slot > (SIZE_MAX - frame->frame_size) / 8U) {
            return false;
        }
        incoming_offset = frame->frame_size + stack_slot * 8U;
        if (!minic_riscv64_emit_sp_load64(file, "t0", incoming_offset)) {
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

static bool emit_sp_store_chunk(FILE *file,
                                const char *source_register,
                                size_t offset,
                                size_t size) {
    const char *opcode;
    size_t byte_index;

    if (file == NULL || source_register == NULL || size == 0U || size > 8U) {
        return false;
    }
    if (size == 8U) {
        return minic_riscv64_emit_sp_store64(file, source_register, offset);
    }
    opcode = size == 4U ? "sw" : size == 2U ? "sh" : size == 1U ? "sb" : NULL;
    if (opcode != NULL) {
        if (offset <= 2047U) {
            return fprintf(file, "  %s %s, %zu(sp)\n", opcode, source_register, offset) >= 0;
        }
        return emit_sp_address(file, "t3", offset) &&
               fprintf(file, "  %s %s, 0(t3)\n", opcode, source_register) >= 0;
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
        if (byte_offset <= 2047U) {
            if (fprintf(file, "  sb t1, %zu(sp)\n", byte_offset) < 0) {
                return false;
            }
        } else if (!emit_sp_address(file, "t3", byte_offset) ||
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
    size_t object_offset;
    size_t chunk_index;

    if (file == NULL || program == NULL || function == NULL || frame == NULL ||
        instruction == NULL || instruction->kind != MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
        return false;
    }
    object_id = instruction->value.parameter_object.object_id;
    if (!core_parameter_location(program,
                                 function,
                                 instruction->value.parameter_object.parameter_index,
                                 &location) ||
        location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
        location.value.slot_count == 0U || location.value.slot_count > 2U ||
        location.value.slot_count != location.integer_register_count + location.stack_slot_count ||
        object_id >= function->object_count ||
        !minic_type_equal(function->objects[object_id].type,
                          function->parameter_types[
                              instruction->value.parameter_object.parameter_index]) ||
        !core_object_offset(program, function, object_id, &object_offset)) {
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
            size_t incoming_offset;

            stack_slot = location.stack_slot_begin +
                         (chunk_index - location.integer_register_count);
            if (stack_slot > (SIZE_MAX - frame->frame_size) / 8U) {
                return false;
            }
            incoming_offset = frame->frame_size + stack_slot * 8U;
            if (!minic_riscv64_emit_sp_load64(file, "t0", incoming_offset)) {
                return false;
            }
        }
        chunk_offset = chunk_index * 8U;
        if (chunk_offset >= location.value.storage_size || object_offset > SIZE_MAX - chunk_offset) {
            return false;
        }
        chunk_size = location.value.storage_size - chunk_offset;
        if (chunk_size > 8U) {
            chunk_size = 8U;
        }
        if (!emit_sp_store_chunk(
                file, source_register, object_offset + chunk_offset, chunk_size)) {
            return false;
        }
    }
    return true;
}

'''
text = replace_function(text,
                        emit_parameter_start,
                        emit_call_marker,
                        new_parameter_helpers,
                        'core-codegen parameter ingress')
text = replace_once(
    text,
    '''    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return emit_parameter(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
''',
    '''    case MINIC_CORE_INSTRUCTION_PARAMETER:
        return emit_parameter(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return emit_parameter_object(file, program, function, frame, instruction);
    case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS:
''',
    'core-codegen parameter object emission',
)

new_emit_terminator = r'''static bool emit_terminator(FILE *file,
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
                return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                return_value.slot_count == 0U || return_value.slot_count > 2U ||
                !core_object_offset(
                    program, function, terminator->return_object, &object_offset) ||
                !emit_sp_address(file, "t0", object_offset) ||
                !minic_riscv64_emit_integer_aggregate_load_chunk(
                    file, program, function->return_type, 0U, "a0", "t0") ||
                (return_value.slot_count == 2U &&
                 !minic_riscv64_emit_integer_aggregate_load_chunk(
                     file, program, function->return_type, 1U, "a1", "t0"))) {
                return false;
            }
        } else if (terminator->return_value != MINIC_CORE_VALUE_INVALID &&
                   !load_core_value(file, frame, terminator->return_value, "a0")) {
            return false;
        }
        return fprintf(file, "  j .L%s_core_return\n", symbol_name) >= 0;
    case MINIC_CORE_TERMINATOR_BRANCH:
        return fprintf(
                   file, "  j .L%s_core_bb%" PRIu32 "\n", symbol_name, terminator->branch_target) >=
               0;
    case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH:
        if (!load_core_value(file, frame, terminator->conditional.condition, "t0") ||
            fprintf(file,
                    "  bnez t0, .L%s_core_bb%" PRIu32 "\n"
                    "  j .L%s_core_bb%" PRIu32 "\n",
                    symbol_name,
                    terminator->conditional.when_true,
                    symbol_name,
                    terminator->conditional.when_false) < 0) {
            return false;
        }
        return true;
    }
    return false;
}

'''
text = replace_function(text,
                        'static bool emit_terminator(FILE *file,',
                        'static bool emit_core_function_basic_v0_with_symbol',
                        new_emit_terminator,
                        'core-codegen terminator')
text = replace_once(
    text,
    '            !emit_terminator(file, function, &frame, symbol_name, &block->terminator)) {\n',
    '            !emit_terminator(file, program, function, &frame, symbol_name, &block->terminator)) {\n',
    'core-codegen terminator call',
)
p.write_text(text)

print('M30_PATCH_APPLIED')
