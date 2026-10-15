#!/usr/bin/env python3
from pathlib import Path

marker = 'M151_INDIRECT_CALL_BATCH_OWNER'

# M151 intentionally runs after M150. M150 adds the variadic bit to Core
# signatures; M151 replaces the fragile first lowerer widening and extends the
# same shared indirect-call owner to fixed by-value record arguments.

# 1) Core IR: indirect signatures accept the same fixed scalar/record parameter
# domain as direct callees. Variadic tails remain scalar VALUE arguments.
path = Path('src/core/core_ir.c')
text = path.read_text()
if marker in text:
    print('M151 indirect call batch already staged')
    raise SystemExit(0)
if 'M150_INDIRECT_VARIADIC_CALL_SIGNATURE_OWNER' not in Path('src/core/core_ir.h').read_text():
    raise SystemExit('M151 requires staged M150 signature metadata')

# Patch only the add_call_signature function's fixed-parameter validator.
start = text.find('bool minic_core_function_add_call_signature(')
end = text.find('\nbool minic_core_function_append_call_arguments(', start)
if start < 0 or end < 0:
    raise SystemExit('M151 could not locate add_call_signature')
body = text[start:end]
old = '''    for (index = 0U; index < parameter_count; ++index) {\n        if (!core_call_scalar_type(parameter_types[index])) {\n            return false;\n        }\n    }'''
new = '''    for (index = 0U; index < parameter_count; ++index) {\n        if (!core_call_parameter_type(parameter_types[index])) {\n            return false;\n        }\n    }'''
if body.count(old) != 1:
    raise SystemExit(f'M151 expected one call-signature parameter validator, found {body.count(old)}')
body = body.replace(old, new, 1)
text = text[:start] + body + text[end:]

# Replace the staged M150 indirect argument verifier with scalar/record fixed
# prefix handling plus scalar variadic tail handling.
old = '''            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {\n                return false;\n            }\n            value_id = argument->value.value_id;\n            if (value_id >= function->value_count || !available_values[value_id]) {\n                return false;\n            }\n            if (parameter_index >= signature->parameter_count) {\n                if (!signature->is_variadic ||\n                    !core_call_scalar_type(function->values[value_id].type)) {\n                    return false;\n                }\n                continue;\n            }\n            if (!minic_type_equal(function->values[value_id].type,\n                                  signature->parameter_types[parameter_index])) {\n                return false;\n            }'''
new = '''            if (parameter_index >= signature->parameter_count) {\n                MinicCoreValueId value_id;\n\n                if (!signature->is_variadic ||\n                    argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {\n                    return false;\n                }\n                value_id = argument->value.value_id;\n                if (value_id >= function->value_count || !available_values[value_id] ||\n                    !core_call_scalar_type(function->values[value_id].type)) {\n                    return false;\n                }\n                continue;\n            }\n            {\n                MinicType parameter_type = signature->parameter_types[parameter_index];\n\n                if (core_call_scalar_type(parameter_type)) {\n                    MinicCoreValueId value_id;\n\n                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {\n                        return false;\n                    }\n                    value_id = argument->value.value_id;\n                    if (value_id >= function->value_count || !available_values[value_id] ||\n                        !minic_type_equal(function->values[value_id].type, parameter_type)) {\n                        return false;\n                    }\n                } else if (minic_type_is_record(parameter_type)) {\n                    MinicCoreObjectId object_id;\n\n                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {\n                        return false;\n                    }\n                    object_id = argument->value.object_id;\n                    if (object_id >= function->object_count ||\n                        !minic_type_equal(function->objects[object_id].type, parameter_type)) {\n                        return false;\n                    }\n                } else {\n                    return false;\n                }\n            }'''
if text.count(old) != 1:
    raise SystemExit(f'M151 expected one staged indirect argument verifier, found {text.count(old)}')
text = text.replace(old, new, 1)

# The function-level signature verifier must accept record fixed parameters too.
old = '''        for (parameter_index = 0U; parameter_index < signature->parameter_count;\n             ++parameter_index) {\n            if (!core_call_scalar_type(signature->parameter_types[parameter_index])) {\n                return false;\n            }\n        }'''
new = '''        for (parameter_index = 0U; parameter_index < signature->parameter_count;\n             ++parameter_index) {\n            if (!core_call_parameter_type(signature->parameter_types[parameter_index])) {\n                return false;\n            }\n        }'''
if text.count(old) != 1:
    raise SystemExit(f'M151 expected one signature-table parameter verifier, found {text.count(old)}')
text = text.replace(old, new, 1)

# Core IR printing is diagnostic but must remain total for OBJECT arguments.
old = '''            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||\n                (argument_index != 0U && fprintf(output, \", \") < 0) ||\n                fprintf(output, \"%%%\" PRIu32, argument->value.value_id) < 0) {\n                return false;\n            }'''
new = '''            if (argument_index != 0U && fprintf(output, \", \") < 0) {\n                return false;\n            }\n            if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {\n                if (fprintf(output, \"%%%\" PRIu32, argument->value.value_id) < 0) {\n                    return false;\n                }\n            } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {\n                if (fprintf(output, \"%%o%\" PRIu32, argument->value.object_id) < 0) {\n                    return false;\n                }\n            } else {\n                return false;\n            }'''
if text.count(old) != 1:
    raise SystemExit(f'M151 expected one indirect printer argument form, found {text.count(old)}')
text = text.replace(old, new, 1)

# Add a durable semantic marker next to the signature table validation.
needle = '    for (index = 0U; index < function->call_signature_count; ++index) {'
if text.count(needle) != 1:
    raise SystemExit('M151 could not mark call-signature owner')
text = text.replace(needle,
                    '    /* M151_INDIRECT_CALL_BATCH_OWNER: indirect fixed parameters share the direct scalar/record domain. */\n' + needle,
                    1)
path.write_text(text)

# 2) Core lowerer: replace the whole staged indirect-call lowerer. This also
# fixes M150's broad textual replacement bug, which accidentally converted the
# fixed-prefix test into `argument_index < argument_count`.
path = Path('src/core/core_lower.c')
text = path.read_text()
start = text.find('static MinicCoreLowerStatus lower_indirect_call(')
end = text.find('\nstatic MinicCoreLowerStatus lower_expression(', start)
if start < 0 or end < 0:
    raise SystemExit('M151 could not locate lower_indirect_call bounds')
new_func = r'''static MinicCoreLowerStatus lower_indirect_call(MinicCoreLowerContext *context,
                                                const MinicExpression *expression,
                                                MinicCoreValueId *value_id) {
    const MinicExpression *callee_expression;
    const MinicFunctionType *signature;
    MinicCoreCallSignatureId signature_id;
    MinicCoreInstruction instruction;
    MinicCoreValueId callee_value;
    MinicCoreCallArgument *arguments;
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType argument_types[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicCoreLowerStatus status;
    MinicExpressionId callee_value_expression_id;
    MinicType callee_value_type;
    MinicType function_type;
    size_t argument_begin;
    size_t argument_count;
    size_t argument_index;
    bool returns_void;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL ||
        expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    callee_value_expression_id = expression->value.call.callee;
    callee_expression =
        minic_c0_program_expression(context->body->program, callee_value_expression_id);
    if (callee_expression != NULL &&
        callee_expression->kind == MINIC_EXPRESSION_DEREFERENCE &&
        minic_type_is_function(callee_expression->type)) {
        const MinicExpression *pointer_operand;

        callee_value_expression_id = callee_expression->value.unary.operand;
        pointer_operand = minic_c0_program_expression(
            context->body->program, callee_value_expression_id);
        if (pointer_operand == NULL ||
            !core_scalar_expression_value_type(
                context->body, pointer_operand, &callee_value_type) ||
            !minic_type_pointee(callee_value_type, &function_type) ||
            !minic_type_is_function(function_type) ||
            !minic_type_equal(function_type, callee_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    } else if (callee_expression == NULL ||
               !core_scalar_expression_value_type(
                   context->body, callee_expression, &callee_value_type) ||
               !minic_type_pointee(callee_value_type, &function_type) ||
               !minic_type_is_function(function_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    signature = minic_c0_program_function_type(
        context->body->program, function_type.function_type_id);
    argument_count = expression->value.call.argument_count;
    if (signature == NULL || argument_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (!signature->is_variadic && argument_count != signature->parameter_count) ||
        (signature->is_variadic && argument_count < signature->parameter_count) ||
        !minic_type_equal(expression->type, signature->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    returns_void = minic_type_is_void(signature->return_type);
    if (!returns_void && !core_memory_scalar_type(signature->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    arguments = argument_count == 0U
                    ? NULL
                    : (MinicCoreCallArgument *)calloc(argument_count, sizeof(*arguments));
    if (argument_count != 0U && arguments == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    /* M151_INDIRECT_CALL_BATCH_OWNER: fixed arguments use the same scalar or
       address-backed record transport as direct calls. A variadic tail keeps
       the actual scalar type. Every VALUE is spilled until the callee has been
       evaluated so the final indirect call block owns all SSA inputs. */
    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        if (argument_index < signature->parameter_count) {
            argument_types[argument_index] = signature->parameter_types[argument_index];
            if (minic_type_is_record(argument_types[argument_index])) {
                MinicCoreObjectId object_id;

                status = lower_record_call_argument_object(
                    context,
                    expression->value.call.arguments[argument_index],
                    argument_types[argument_index],
                    &object_id);
                if (status != MINIC_CORE_LOWER_OK) {
                    free(arguments);
                    return status;
                }
                arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
                arguments[argument_index].value.object_id = object_id;
                continue;
            }
            if (!core_memory_scalar_type(argument_types[argument_index])) {
                free(arguments);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
            status = lower_scalar_assignment_value(
                context,
                argument_types[argument_index],
                expression->value.call.arguments[argument_index],
                &arguments[argument_index].value.value_id);
        } else {
            const MinicExpression *argument_expression = minic_c0_program_expression(
                context->body->program, expression->value.call.arguments[argument_index]);

            if (argument_expression == NULL ||
                !core_scalar_expression_value_type(
                    context->body, argument_expression, &argument_types[argument_index]) ||
                !core_memory_scalar_type(argument_types[argument_index])) {
                free(arguments);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
            status = lower_expression(context,
                                      expression->value.call.arguments[argument_index],
                                      &arguments[argument_index].value.value_id);
        }
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
        if (arguments[argument_index].value.value_id >= context->function->value_count ||
            !minic_type_equal(
                context->function->values[arguments[argument_index].value.value_id].type,
                argument_types[argument_index])) {
            free(arguments);
            return MINIC_CORE_LOWER_ERROR;
        }
        status = spill_scalar_value(context,
                                    expression->span,
                                    argument_types[argument_index],
                                    arguments[argument_index].value.value_id,
                                    &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }

    status = lower_expression(context, callee_value_expression_id, &callee_value);
    if (status != MINIC_CORE_LOWER_OK) {
        free(arguments);
        return status;
    }
    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        if (arguments[argument_index].kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            continue;
        }
        status = reload_scalar_value(context,
                                     expression->span,
                                     argument_types[argument_index],
                                     argument_objects[argument_index],
                                     &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    if (callee_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[callee_value].type, callee_value_type)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_core_function_add_call_signature(context->function,
                                                function_type.function_type_id,
                                                signature->return_type,
                                                signature->parameter_types,
                                                signature->parameter_count,
                                                signature->is_variadic,
                                                &signature_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, argument_count, &argument_begin)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    free(arguments);

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
    instruction.span = expression->span;
    instruction.type = signature->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.indirect_call.callee = callee_value;
    instruction.value.indirect_call.signature_id = signature_id;
    instruction.value.indirect_call.argument_begin = argument_begin;
    instruction.value.indirect_call.argument_count = argument_count;
    if (returns_void) {
        *value_id = MINIC_CORE_VALUE_INVALID;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &instruction)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}
'''
text = text[:start] + new_func + text[end:]
path.write_text(text)

# 3) RV64: use the shared ABI classifier for indirect calls too. The previous
# indirect path assumed one scalar == one a-register; that is exactly what made
# fixed record arguments impossible even though direct calls already knew the
# aggregate ABI.
path = Path('src/target/riscv64/core_codegen.c')
text = path.read_text()
insert_at = text.find('static bool core_instruction_supported(')
if insert_at < 0:
    raise SystemExit('M151 could not locate core_instruction_supported')
helper = r'''/* M151_INDIRECT_CALL_BATCH_OWNER: validate indirect arguments through the
   same register-only RV64 ABI classifier used by direct calls. */
static bool core_indirect_call_supported(const MinicC0Program *program,
                                         const MinicCoreFunction *function,
                                         const MinicCoreInstruction *instruction) {
    const MinicCoreCallSignature *signature;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    MinicType function_type;
    size_t argument_index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_INDIRECT_CALL ||
        instruction->value.indirect_call.signature_id >= function->call_signature_count ||
        instruction->value.indirect_call.callee >= function->value_count ||
        instruction->value.indirect_call.argument_begin > function->call_argument_count ||
        instruction->value.indirect_call.argument_count >
            function->call_argument_count - instruction->value.indirect_call.argument_begin ||
        !minic_type_pointee(
            function->values[instruction->value.indirect_call.callee].type, &function_type) ||
        !minic_type_is_function(function_type)) {
        return false;
    }
    signature = &function->call_signatures[instruction->value.indirect_call.signature_id];
    if (function_type.function_type_id != signature->function_type_id ||
        (!signature->is_variadic &&
         instruction->value.indirect_call.argument_count != signature->parameter_count) ||
        (signature->is_variadic &&
         instruction->value.indirect_call.argument_count < signature->parameter_count)) {
        return false;
    }
    if (program == NULL) {
        if ((!minic_type_is_void(signature->return_type) &&
             !core_scalar_type(signature->return_type)) ||
            instruction->value.indirect_call.argument_count > 8U) {
            return false;
        }
        for (argument_index = 0U;
             argument_index < instruction->value.indirect_call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.indirect_call.argument_begin + argument_index];
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count) {
                return false;
            }
            if (argument_index < signature->parameter_count) {
                if (!core_scalar_type(signature->parameter_types[argument_index])) {
                    return false;
                }
            } else if (!signature->is_variadic ||
                       !core_scalar_type(function->values[argument->value.value_id].type)) {
                return false;
            }
        }
        return true;
    }
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, signature->return_type, &cursor, &return_value) ||
        (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
         return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER)) {
        return false;
    }
    for (argument_index = 0U;
         argument_index < instruction->value.indirect_call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.indirect_call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType argument_type;
        bool is_fixed_parameter = argument_index < signature->parameter_count;

        if (is_fixed_parameter) {
            argument_type = signature->parameter_types[argument_index];
        } else {
            if (!signature->is_variadic ||
                argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count) {
                return false;
            }
            argument_type = function->values[argument->value.value_id].type;
            if (!core_scalar_type(argument_type)) {
                return false;
            }
        }
        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location) ||
            location.floating_register_count != 0U || location.stack_slot_count != 0U) {
            return false;
        }
        if (core_scalar_type(argument_type)) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count ||
                location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
                location.integer_register_count != 1U ||
                location.integer_register_begin >= 8U) {
                return false;
            }
        } else if (is_fixed_parameter && minic_type_is_record(argument_type)) {
            MinicCoreObjectId object_id;
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT ||
                location.value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
                location.value.slot_count == 0U || location.value.slot_count > 2U ||
                location.integer_register_count != location.value.slot_count ||
                location.integer_register_begin + location.integer_register_count > 8U) {
                return false;
            }
            object_id = argument->value.object_id;
            if (object_id >= function->object_count ||
                !minic_type_equal(function->objects[object_id].type, argument_type)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

'''
text = text[:insert_at] + helper + text[insert_at:]
old = '''    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {\n        const MinicCoreCallSignature *signature;\n        MinicType function_type;\n\n        if (instruction->value.indirect_call.signature_id >= function->call_signature_count ||\n            instruction->value.indirect_call.callee >= function->value_count ||\n            instruction->value.indirect_call.argument_count > 8U) {\n            return false;\n        }\n        signature =\n            &function->call_signatures[instruction->value.indirect_call.signature_id];\n        return signature->parameter_count <= 8U &&\n               ((!signature->is_variadic &&\n                 instruction->value.indirect_call.argument_count == signature->parameter_count) ||\n                (signature->is_variadic &&\n                 instruction->value.indirect_call.argument_count >= signature->parameter_count)) &&\n               minic_type_pointee(\n                   function->values[instruction->value.indirect_call.callee].type,\n                   &function_type) &&\n               minic_type_is_function(function_type) &&\n               function_type.function_type_id == signature->function_type_id;\n    }'''
new = '''    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL:\n        return core_indirect_call_supported(program, function, instruction);'''
if text.count(old) != 1:
    raise SystemExit(f'M151 expected one staged indirect supported case, found {text.count(old)}')
text = text.replace(old, new, 1)

# Replace the old scalar-register-index emitter with ABI placement.
start = text.find('static bool emit_indirect_call(')
end = text.find('\nstatic bool emit_field_address(', start)
if start < 0 or end < 0:
    raise SystemExit('M151 could not locate emit_indirect_call bounds')
new_emit = r'''static bool emit_indirect_call(FILE *file,
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
        !core_indirect_call_supported(program, function, instruction)) {
        return false;
    }
    signature = &function->call_signatures[instruction->value.indirect_call.signature_id];
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, signature->return_type, &cursor, &return_value)) {
        return false;
    }
    (void)return_value;
    for (argument_index = 0U;
         argument_index < instruction->value.indirect_call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.indirect_call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType argument_type;
        bool is_fixed_parameter = argument_index < signature->parameter_count;

        if (is_fixed_parameter) {
            argument_type = signature->parameter_types[argument_index];
        } else {
            argument_type = function->values[argument->value.value_id].type;
        }
        if (!minic_riscv64_abi_place_argument(
                program, argument_type, is_fixed_parameter, &cursor, &location)) {
            return false;
        }
        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
            if (location.integer_register_count != 1U || location.integer_register_begin >= 8U ||
                !load_core_value(file,
                                 frame,
                                 argument->value.value_id,
                                 minic_core_rv64_argument_registers[location.integer_register_begin])) {
                return false;
            }
            continue;
        }
        if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            size_t chunk_index;
            size_t object_offset;

            if (!is_fixed_parameter ||
                !core_object_offset(program, function, argument->value.object_id, &object_offset)) {
                return false;
            }
            for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
                size_t chunk_offset = chunk_index * 8U;
                size_t chunk_size;
                size_t register_index = location.integer_register_begin + chunk_index;

                if (chunk_offset >= location.value.storage_size || register_index >= 8U ||
                    object_offset > SIZE_MAX - chunk_offset) {
                    return false;
                }
                chunk_size = location.value.storage_size - chunk_offset;
                if (chunk_size > 8U) {
                    chunk_size = 8U;
                }
                if (!emit_sp_load_chunk(file,
                                        minic_core_rv64_argument_registers[register_index],
                                        object_offset + chunk_offset,
                                        chunk_size)) {
                    return false;
                }
            }
            continue;
        }
        return false;
    }
    if (!load_core_value(file, frame, instruction->value.indirect_call.callee, "t0") ||
        fprintf(file, "  jalr ra, t0, 0\n") < 0) {
        return false;
    }
    if (minic_type_is_void(instruction->type)) {
        return true;
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "a0")) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "a0");
}
'''
text = text[:start] + new_emit + text[end:]
path.write_text(text)

# 4) Focused regressions deliberately cover both halves of the shared owner.
path = Path('tests/compiler/c0/m151_indirect_call_batch.c')
path.write_text(r'''typedef struct Pair {
    unsigned long pointer_bits;
    unsigned long length;
} Pair;

typedef int (*record_fn)(int, Pair);
typedef int (*variadic_fn)(const char *, ...);

static int consume_record(int bias, Pair pair) {
    return bias + (int)pair.length;
}

static int invoke_record(record_fn fn, Pair pair) {
    return fn(3, pair);
}

static int consume_variadic(const char *tag, ...) {
    return tag[0];
}

static int invoke_variadic(variadic_fn fn, int value, void *pointer) {
    return fn("v", value, pointer);
}

int main(void) {
    Pair pair = {0, 4};
    int a = invoke_record(consume_record, pair);
    int b = invoke_variadic(consume_variadic, 7, (void *)0);
    return a == 7 && b == 'v' ? 0 : 1;
}
''')

print('M151 indirect variadic+record call batch staged')
