#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"anchor not found in {path}")
    p.write_text(text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# Core IR: direct callees preserve whether the source function is variadic.
# Fixed parameter types remain the signature prefix; variadic tail argument
# types are carried by their VALUE Core values, avoiding a second type table.
# ---------------------------------------------------------------------------
path = "src/core/core_ir.h"
replace_once(
    path,
    '''typedef struct MinicCoreCallee {
    char *name;
    size_t name_length;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
} MinicCoreCallee;
''',
    '''typedef struct MinicCoreCallee {
    char *name;
    size_t name_length;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
    /* BATCH_D_VARIADIC_DIRECT_CALL: parameter_types is the fixed prefix;
       variadic tail types are the semantic types of VALUE arguments. */
    bool is_variadic;
} MinicCoreCallee;
''')
replace_once(
    path,
    '''bool minic_core_function_add_callee(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    MinicCoreCalleeId *callee_id);
''',
    '''bool minic_core_function_add_callee(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    bool is_variadic,
                                    MinicCoreCalleeId *callee_id);
''')

path = "src/core/core_ir.c"
replace_once(
    path,
    '''static bool callee_signature_equal(const MinicCoreCallee *callee,
                                   const char *name,
                                   size_t name_length,
                                   MinicType return_type,
                                   const MinicType *parameter_types,
                                   size_t parameter_count) {
''',
    '''static bool callee_signature_equal(const MinicCoreCallee *callee,
                                   const char *name,
                                   size_t name_length,
                                   MinicType return_type,
                                   const MinicType *parameter_types,
                                   size_t parameter_count,
                                   bool is_variadic) {
''')
replace_once(
    path,
    '''        !minic_type_equal(callee->return_type, return_type) ||
        callee->parameter_count != parameter_count) {
''',
    '''        !minic_type_equal(callee->return_type, return_type) ||
        callee->parameter_count != parameter_count || callee->is_variadic != is_variadic) {
''')
replace_once(
    path,
    '''bool minic_core_function_add_callee(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    MinicCoreCalleeId *callee_id) {
''',
    '''bool minic_core_function_add_callee(MinicCoreFunction *function,
                                    const char *name,
                                    size_t name_length,
                                    MinicType return_type,
                                    const MinicType *parameter_types,
                                    size_t parameter_count,
                                    bool is_variadic,
                                    MinicCoreCalleeId *callee_id) {
''')
replace_once(
    path,
    '''            if (!callee_signature_equal(
                    existing, name, name_length, return_type, parameter_types, parameter_count)) {
''',
    '''            if (!callee_signature_equal(existing,
                                        name,
                                        name_length,
                                        return_type,
                                        parameter_types,
                                        parameter_count,
                                        is_variadic)) {
''')
replace_once(
    path,
    '''    stored.return_type = return_type;
    stored.parameter_count = parameter_count;
''',
    '''    stored.return_type = return_type;
    stored.parameter_count = parameter_count;
    stored.is_variadic = is_variadic;
''')

old_call_verify = '''    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
        size_t argument_index;
        size_t argument_end;
        bool returns_void;

        if (instruction->value.call.callee_id >= function->callee_count ||
            instruction->value.call.argument_begin > function->call_argument_count ||
            instruction->value.call.argument_count >
                function->call_argument_count - instruction->value.call.argument_begin) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        if (instruction->value.call.argument_count != callee->parameter_count ||
            !minic_type_equal(instruction->type, callee->return_type)) {
            return false;
        }
        returns_void = minic_type_is_void(callee->return_type);
        if (returns_void) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID) {
                return false;
            }
        } else if (minic_type_is_record(callee->return_type)) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.call.result_object >= function->object_count ||
                !minic_type_equal(
                    function->objects[instruction->value.call.result_object].type,
                    callee->return_type)) {
                return false;
            }
        } else if (!core_call_scalar_type(callee->return_type) ||
                   instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID ||
                   !instruction_result_is_valid(function, instruction)) {
            return false;
        }
        argument_end =
            instruction->value.call.argument_begin + instruction->value.call.argument_count;
        for (argument_index = instruction->value.call.argument_begin; argument_index < argument_end;
             ++argument_index) {
            const MinicCoreCallArgument *argument;
            MinicType parameter_type;
            size_t parameter_index;

            argument = &function->call_arguments[argument_index];
            parameter_index = argument_index - instruction->value.call.argument_begin;
            parameter_type = callee->parameter_types[parameter_index];
            if (core_call_scalar_type(parameter_type)) {
                MinicCoreValueId value_id;

                if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                    return false;
                }
                value_id = argument->value.value_id;
                if (value_id >= function->value_count || !available_values[value_id] ||
                    !minic_type_equal(function->values[value_id].type, parameter_type)) {
                    return false;
                }
            } else if (minic_type_is_record(parameter_type)) {
                MinicCoreObjectId object_id;

                if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    return false;
                }
                object_id = argument->value.object_id;
                if (object_id >= function->object_count ||
                    !minic_type_equal(function->objects[object_id].type, parameter_type)) {
                    return false;
                }
            } else {
                return false;
            }
        }
        return true;
    }
'''
new_call_verify = '''    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
        size_t argument_index;
        size_t argument_end;
        bool returns_void;

        if (instruction->value.call.callee_id >= function->callee_count ||
            instruction->value.call.argument_begin > function->call_argument_count ||
            instruction->value.call.argument_count >
                function->call_argument_count - instruction->value.call.argument_begin) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        if ((!callee->is_variadic &&
             instruction->value.call.argument_count != callee->parameter_count) ||
            (callee->is_variadic &&
             instruction->value.call.argument_count < callee->parameter_count) ||
            !minic_type_equal(instruction->type, callee->return_type)) {
            return false;
        }
        returns_void = minic_type_is_void(callee->return_type);
        if (returns_void) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID) {
                return false;
            }
        } else if (minic_type_is_record(callee->return_type)) {
            if (instruction->result != MINIC_CORE_VALUE_INVALID ||
                instruction->value.call.result_object >= function->object_count ||
                !minic_type_equal(
                    function->objects[instruction->value.call.result_object].type,
                    callee->return_type)) {
                return false;
            }
        } else if (!core_call_scalar_type(callee->return_type) ||
                   instruction->value.call.result_object != MINIC_CORE_OBJECT_INVALID ||
                   !instruction_result_is_valid(function, instruction)) {
            return false;
        }
        argument_end =
            instruction->value.call.argument_begin + instruction->value.call.argument_count;
        for (argument_index = instruction->value.call.argument_begin; argument_index < argument_end;
             ++argument_index) {
            const MinicCoreCallArgument *argument;
            size_t parameter_index;

            argument = &function->call_arguments[argument_index];
            parameter_index = argument_index - instruction->value.call.argument_begin;
            if (parameter_index >= callee->parameter_count) {
                MinicCoreValueId value_id;

                if (!callee->is_variadic || argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                    return false;
                }
                value_id = argument->value.value_id;
                if (value_id >= function->value_count || !available_values[value_id] ||
                    !core_call_scalar_type(function->values[value_id].type)) {
                    return false;
                }
                continue;
            }
            {
                MinicType parameter_type = callee->parameter_types[parameter_index];

                if (core_call_scalar_type(parameter_type)) {
                    MinicCoreValueId value_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                        return false;
                    }
                    value_id = argument->value.value_id;
                    if (value_id >= function->value_count || !available_values[value_id] ||
                        !minic_type_equal(function->values[value_id].type, parameter_type)) {
                        return false;
                    }
                } else if (minic_type_is_record(parameter_type)) {
                    MinicCoreObjectId object_id;

                    if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                        return false;
                    }
                    object_id = argument->value.object_id;
                    if (object_id >= function->object_count ||
                        !minic_type_equal(function->objects[object_id].type, parameter_type)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
        }
        return true;
    }
'''
replace_once(path, old_call_verify, new_call_verify)

# ---------------------------------------------------------------------------
# AST -> Core: preserve the fixed prefix's assignment conversions. For the
# variadic tail, frontend has already performed C default argument promotions;
# lower the semantic expression as-is and preserve its resulting scalar type.
# ---------------------------------------------------------------------------
path = "src/core/core_lower.c"
old_direct_call = '''static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,
                                              const MinicExpression *expression,
                                              MinicCoreValueId *value_id) {
    const MinicFunction *callee;
    const char *callee_name;
    size_t callee_name_length;
    MinicCoreCalleeId callee_id;
    MinicCoreInstruction instruction;
    MinicCoreCallArgument *arguments;
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicCoreLowerStatus status;
    size_t argument_begin;
    size_t argument_index;
    bool returns_void;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    callee = minic_c0_program_function(context->body->program, expression->value.call.function_id);
    if (callee == NULL || callee->name == NULL || callee->name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    callee_name = callee->assembler_name != NULL ? callee->assembler_name : callee->name;
    callee_name_length =
        callee->assembler_name != NULL ? callee->assembler_name_length : callee->name_length;
    if (callee_name == NULL || callee_name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    returns_void = minic_type_is_void(callee->return_type);
    if (callee->is_variadic || expression->value.call.argument_count != callee->parameter_count ||
        (!returns_void && !core_memory_scalar_type(callee->return_type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        if (!core_memory_scalar_type(callee->parameter_types[argument_index]) &&
            !minic_type_is_record(callee->parameter_types[argument_index])) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }
    arguments = callee->parameter_count == 0U
                    ? NULL
                    : (MinicCoreCallArgument *)calloc(callee->parameter_count, sizeof(*arguments));
    if (callee->parameter_count != 0U && arguments == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        if (minic_type_is_record(callee->parameter_types[argument_index])) {
            MinicCoreObjectId object_id;

            status = lower_record_call_argument_object(
                context,
                expression->value.call.arguments[argument_index],
                callee->parameter_types[argument_index],
                &object_id);
            if (status != MINIC_CORE_LOWER_OK) {
                free(arguments);
                return status;
            }
            arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
            arguments[argument_index].value.object_id = object_id;
            continue;
        }
        arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        status = lower_scalar_assignment_value(
            context,
            callee->parameter_types[argument_index],
            expression->value.call.arguments[argument_index],
            &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=direct-call callee=%s arg=%zu reason=argument-lower status=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          callee_name, argument_index, (int)status);
            free(arguments);
            return status;
        }
        if (arguments[argument_index].value.value_id >= context->function->value_count ||
            !minic_type_equal(
                context->function->values[arguments[argument_index].value.value_id].type,
                callee->parameter_types[argument_index])) {
            free(arguments);
            return MINIC_CORE_LOWER_ERROR;
        }
        status = spill_scalar_value(context,
                                    expression->span,
                                    callee->parameter_types[argument_index],
                                    arguments[argument_index].value.value_id,
                                    &argument_objects[argument_index]);
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr, "CORE_LOWER_DETAIL marker=M90_HOT_ERROR_DETAIL function=%s stage=direct-call callee=%s arg=%zu reason=argument-spill status=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          callee_name, argument_index, (int)status);
            free(arguments);
            return status;
        }
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        if (arguments[argument_index].kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
            continue;
        }
        status = reload_scalar_value(context,
                                     expression->span,
                                     callee->parameter_types[argument_index],
                                     argument_objects[argument_index],
                                     &arguments[argument_index].value.value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
    }
    if (!minic_core_function_add_callee(context->function,
                                        callee_name,
                                        callee_name_length,
                                        callee->return_type,
                                        callee->parameter_types,
                                        callee->parameter_count,
                                        &callee_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, callee->parameter_count, &argument_begin)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    free(arguments);
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.span = expression->span;
    instruction.type = callee->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = callee->parameter_count;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
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
new_direct_call = '''static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,
                                              const MinicExpression *expression,
                                              MinicCoreValueId *value_id) {
    const MinicFunction *callee;
    const char *callee_name;
    size_t callee_name_length;
    MinicCoreCalleeId callee_id;
    MinicCoreInstruction instruction;
    MinicCoreCallArgument *arguments;
    MinicCoreObjectId argument_objects[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType argument_types[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicCoreLowerStatus status;
    size_t argument_begin;
    size_t argument_count;
    size_t argument_index;
    bool returns_void;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    callee = minic_c0_program_function(context->body->program, expression->value.call.function_id);
    if (callee == NULL || callee->name == NULL || callee->name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    callee_name = callee->assembler_name != NULL ? callee->assembler_name : callee->name;
    callee_name_length =
        callee->assembler_name != NULL ? callee->assembler_name_length : callee->name_length;
    if (callee_name == NULL || callee_name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    argument_count = expression->value.call.argument_count;
    returns_void = minic_type_is_void(callee->return_type);
    if (argument_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (!callee->is_variadic && argument_count != callee->parameter_count) ||
        (callee->is_variadic && argument_count < callee->parameter_count) ||
        (!returns_void && !core_memory_scalar_type(callee->return_type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        if (argument_index < callee->parameter_count) {
            argument_types[argument_index] = callee->parameter_types[argument_index];
            if (!core_memory_scalar_type(argument_types[argument_index]) &&
                !minic_type_is_record(argument_types[argument_index])) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        } else {
            const MinicExpression *argument_expression = minic_c0_program_expression(
                context->body->program, expression->value.call.arguments[argument_index]);
            if (argument_expression == NULL ||
                !core_scalar_expression_value_type(
                    context->body, argument_expression, &argument_types[argument_index]) ||
                !core_memory_scalar_type(argument_types[argument_index])) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
    }
    arguments = argument_count == 0U
                    ? NULL
                    : (MinicCoreCallArgument *)calloc(argument_count, sizeof(*arguments));
    if (argument_count != 0U && arguments == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
        if (argument_index < callee->parameter_count &&
            minic_type_is_record(argument_types[argument_index])) {
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
        arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        if (argument_index < callee->parameter_count) {
            status = lower_scalar_assignment_value(
                context,
                argument_types[argument_index],
                expression->value.call.arguments[argument_index],
                &arguments[argument_index].value.value_id);
        } else {
            status = lower_expression(context,
                                      expression->value.call.arguments[argument_index],
                                      &arguments[argument_index].value.value_id);
        }
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr,
                          "CORE_LOWER_DETAIL marker=BATCH_D_VARIADIC_DIRECT_CALL function=%s "
                          "stage=direct-call callee=%s arg=%zu fixed=%d reason=argument-lower status=%d\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          callee_name,
                          argument_index,
                          argument_index < callee->parameter_count ? 1 : 0,
                          (int)status);
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
    if (!minic_core_function_add_callee(context->function,
                                        callee_name,
                                        callee_name_length,
                                        callee->return_type,
                                        callee->parameter_types,
                                        callee->parameter_count,
                                        callee->is_variadic,
                                        &callee_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, argument_count, &argument_begin)) {
        free(arguments);
        return MINIC_CORE_LOWER_ERROR;
    }
    free(arguments);
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.span = expression->span;
    instruction.type = callee->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = argument_count;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
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
replace_once(path, old_direct_call, new_direct_call)

# Record-return direct calls remain fail-closed for variadic callees for now,
# but the callee API still preserves the source signature bit.
replace_once(
    path,
    '''                                        callee->return_type,
                                        callee->parameter_types,
                                        callee->parameter_count,
                                        &callee_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, callee->parameter_count, &argument_begin)) {
''',
    '''                                        callee->return_type,
                                        callee->parameter_types,
                                        callee->parameter_count,
                                        callee->is_variadic,
                                        &callee_id) ||
        !minic_core_function_append_call_arguments(
            context->function, arguments, callee->parameter_count, &argument_begin)) {
''')

# ---------------------------------------------------------------------------
# RV64 Core backend: fixed prefix uses fixed-parameter ABI rules; variadic tail
# derives its type from the Core VALUE and uses the ABI helper's variadic mode.
# Keep the existing register-only first tier so this batch stays small/focused.
# ---------------------------------------------------------------------------
path = "src/target/riscv64/core_codegen.c"
old_supported = '''static bool core_direct_call_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    const MinicCoreCallee *callee;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_CALL ||
        instruction->value.call.callee_id >= function->callee_count) {
        return false;
    }
    callee = &function->callees[instruction->value.call.callee_id];
    if (callee->name == NULL || callee->name_length == 0U ||
        instruction->value.call.argument_count != callee->parameter_count ||
        instruction->value.call.argument_begin > function->call_argument_count ||
        instruction->value.call.argument_count >
            function->call_argument_count - instruction->value.call.argument_begin) {
        return false;
    }
    if (program == NULL) {
        if ((!minic_type_is_void(callee->return_type) &&
             !core_scalar_type(callee->return_type)) ||
            callee->parameter_count > 8U) {
            return false;
        }
        for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.call.argument_begin + argument_index];
            if (!core_scalar_type(callee->parameter_types[argument_index]) ||
                argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                return false;
            }
        }
        return true;
    }
    /* M86_DIRECT_RECORD_CALL_RESULT: mirror the existing callee-side
       one/two-slot aggregate return ABI on direct call sites. */
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value) ||
        (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
         return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
         (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
          return_value.slot_count == 0U || return_value.slot_count > 2U)) ||
        (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
         (!minic_type_is_record(callee->return_type) ||
          instruction->value.call.result_object >= function->object_count ||
          !minic_type_equal(
              function->objects[instruction->value.call.result_object].type,
              callee->return_type)))) {
        return false;
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType parameter_type = callee->parameter_types[argument_index];

        if (!minic_riscv64_abi_place_argument(
                program, parameter_type, true, &cursor, &location) ||
            location.floating_register_count != 0U || location.stack_slot_count != 0U) {
            return false;
        }
        if (core_scalar_type(parameter_type)) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
                location.integer_register_count != 1U || location.integer_register_begin >= 8U) {
                return false;
            }
        } else if (minic_type_is_record(parameter_type)) {
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
                !minic_type_equal(function->objects[object_id].type, parameter_type)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}
'''
new_supported = '''static bool core_direct_call_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    const MinicCoreCallee *callee;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;

    if (function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_CALL ||
        instruction->value.call.callee_id >= function->callee_count) {
        return false;
    }
    callee = &function->callees[instruction->value.call.callee_id];
    if (callee->name == NULL || callee->name_length == 0U ||
        (!callee->is_variadic &&
         instruction->value.call.argument_count != callee->parameter_count) ||
        (callee->is_variadic &&
         instruction->value.call.argument_count < callee->parameter_count) ||
        instruction->value.call.argument_begin > function->call_argument_count ||
        instruction->value.call.argument_count >
            function->call_argument_count - instruction->value.call.argument_begin) {
        return false;
    }
    if (program == NULL) {
        if ((!minic_type_is_void(callee->return_type) &&
             !core_scalar_type(callee->return_type)) ||
            instruction->value.call.argument_count > 8U) {
            return false;
        }
        for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.call.argument_begin + argument_index];
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                return false;
            }
            if (argument_index < callee->parameter_count) {
                if (!core_scalar_type(callee->parameter_types[argument_index])) {
                    return false;
                }
            } else if (argument->value.value_id >= function->value_count ||
                       !core_scalar_type(function->values[argument->value.value_id].type)) {
                return false;
            }
        }
        return true;
    }
    /* M86_DIRECT_RECORD_CALL_RESULT: mirror the existing callee-side
       one/two-slot aggregate return ABI on direct call sites. */
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value) ||
        (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
         return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
         (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
          return_value.slot_count == 0U || return_value.slot_count > 2U)) ||
        (return_value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
         (!minic_type_is_record(callee->return_type) ||
          instruction->value.call.result_object >= function->object_count ||
          !minic_type_equal(
              function->objects[instruction->value.call.result_object].type,
              callee->return_type)))) {
        return false;
    }
    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType argument_type;
        bool is_fixed_parameter;

        is_fixed_parameter = argument_index < callee->parameter_count;
        if (is_fixed_parameter) {
            argument_type = callee->parameter_types[argument_index];
        } else {
            if (!callee->is_variadic || argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
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
                location.value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER ||
                location.integer_register_count != 1U || location.integer_register_begin >= 8U) {
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
replace_once(path, old_supported, new_supported)

old_emit_loop = '''    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType parameter_type = callee->parameter_types[argument_index];

        if (!minic_riscv64_abi_place_argument(
                program, parameter_type, true, &cursor, &location)) {
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

            if (!core_object_offset(program, function, argument->value.object_id, &object_offset)) {
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
'''
new_emit_loop = '''    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument = &function->call_arguments[
            instruction->value.call.argument_begin + argument_index];
        MinicRiscv64AbiArgumentLocation location;
        MinicType argument_type;
        bool is_fixed_parameter;

        is_fixed_parameter = argument_index < callee->parameter_count;
        if (is_fixed_parameter) {
            argument_type = callee->parameter_types[argument_index];
        } else {
            if (!callee->is_variadic || argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count) {
                return false;
            }
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
'''
replace_once(path, old_emit_loop, new_emit_loop)

print("CORE_BATCH_D_PATCHED direct-variadic-call fixed-prefix-plus-value-tail")
