#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


def replace_between(path: str, start: str, end: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text()
    a = text.find(start)
    if a < 0:
        raise SystemExit(f"{path}: start marker not found: {start[:160]!r}")
    b = text.find(end, a)
    if b < 0:
        raise SystemExit(f"{path}: end marker not found: {end[:160]!r}")
    if text.find(start, a + 1) >= 0:
        raise SystemExit(f"{path}: start marker not unique: {start[:160]!r}")
    p.write_text(text[:a] + replacement + text[b:])


# ---------------------------------------------------------------------------
# Core IR: calls may carry either scalar SSA values or addressable aggregate
# objects.  Aggregate-by-value is deliberately not disguised as a pointer.
# ---------------------------------------------------------------------------
replace_once(
    "src/core/core_ir.h",
    "typedef struct MinicCoreCallee {\n"
    "    char *name;\n"
    "    size_t name_length;\n"
    "    MinicType return_type;\n"
    "    MinicType *parameter_types;\n"
    "    size_t parameter_count;\n"
    "} MinicCoreCallee;\n\n",
    "typedef struct MinicCoreCallee {\n"
    "    char *name;\n"
    "    size_t name_length;\n"
    "    MinicType return_type;\n"
    "    MinicType *parameter_types;\n"
    "    size_t parameter_count;\n"
    "} MinicCoreCallee;\n\n"
    "typedef enum MinicCoreCallArgumentKind {\n"
    "    MINIC_CORE_CALL_ARGUMENT_VALUE = 0,\n"
    "    MINIC_CORE_CALL_ARGUMENT_OBJECT\n"
    "} MinicCoreCallArgumentKind;\n\n"
    "typedef struct MinicCoreCallArgument {\n"
    "    MinicCoreCallArgumentKind kind;\n"
    "    union {\n"
    "        MinicCoreValueId value_id;\n"
    "        MinicCoreObjectId object_id;\n"
    "    } value;\n"
    "} MinicCoreCallArgument;\n\n",
)
replace_once(
    "src/core/core_ir.h",
    "    MinicCoreValueId *call_arguments;\n",
    "    MinicCoreCallArgument *call_arguments;\n",
)
replace_once(
    "src/core/core_ir.h",
    "bool minic_core_function_append_call_arguments(MinicCoreFunction *function,\n"
    "                                               const MinicCoreValueId *arguments,\n",
    "bool minic_core_function_append_call_arguments(MinicCoreFunction *function,\n"
    "                                               const MinicCoreCallArgument *arguments,\n",
)

replace_once(
    "src/core/core_ir.c",
    "        function->global_count >= (size_t)UINT32_MAX ||\n"
    "        (!minic_type_is_integer(type) && !minic_type_is_pointer(type))) {\n",
    "        function->global_count >= (size_t)UINT32_MAX ||\n"
    "        (!minic_type_is_integer(type) && !minic_type_is_pointer(type) &&\n"
    "         !minic_type_is_array(type))) {\n",
)
replace_once(
    "src/core/core_ir.c",
    "static bool core_call_scalar_type(MinicType type) {\n"
    "    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n"
    "}\n",
    "static bool core_call_scalar_type(MinicType type) {\n"
    "    return minic_type_is_integer(type) || minic_type_is_pointer(type);\n"
    "}\n\n"
    "static bool core_call_parameter_type(MinicType type) {\n"
    "    return core_call_scalar_type(type) || minic_type_is_record(type);\n"
    "}\n",
)
replace_once(
    "src/core/core_ir.c",
    "    for (index = 0U; index < parameter_count; ++index) {\n"
    "        if (!core_call_scalar_type(parameter_types[index])) {\n"
    "            return false;\n"
    "        }\n"
    "    }\n",
    "    for (index = 0U; index < parameter_count; ++index) {\n"
    "        if (!core_call_parameter_type(parameter_types[index])) {\n"
    "            return false;\n"
    "        }\n"
    "    }\n",
)
replace_once(
    "src/core/core_ir.c",
    "bool minic_core_function_append_call_arguments(MinicCoreFunction *function,\n"
    "                                               const MinicCoreValueId *arguments,\n",
    "bool minic_core_function_append_call_arguments(MinicCoreFunction *function,\n"
    "                                               const MinicCoreCallArgument *arguments,\n",
)

old_call_verify = r'''        for (argument_index = instruction->value.call.argument_begin; argument_index < argument_end;
             ++argument_index) {
            MinicCoreValueId argument;
            size_t parameter_index;

            argument = function->call_arguments[argument_index];
            parameter_index = argument_index - instruction->value.call.argument_begin;
            if (argument >= function->value_count || !available_values[argument] ||
                !minic_type_equal(function->values[argument].type,
                                  callee->parameter_types[parameter_index])) {
                return false;
            }
        }
'''
new_call_verify = r'''        for (argument_index = instruction->value.call.argument_begin; argument_index < argument_end;
             ++argument_index) {
            const MinicCoreCallArgument *argument;
            MinicType parameter_type;
            size_t parameter_index;

            argument = &function->call_arguments[argument_index];
            parameter_index = argument_index - instruction->value.call.argument_begin;
            parameter_type = callee->parameter_types[parameter_index];
            if (core_call_scalar_type(parameter_type)) {
                if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                    argument->value.value_id >= function->value_count ||
                    !available_values[argument->value.value_id] ||
                    !minic_type_equal(function->values[argument->value.value_id].type,
                                      parameter_type)) {
                    return false;
                }
            } else if (minic_type_is_record(parameter_type)) {
                if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT ||
                    argument->value.object_id >= function->object_count ||
                    !minic_type_equal(function->objects[argument->value.object_id].type,
                                      parameter_type)) {
                    return false;
                }
            } else {
                return false;
            }
        }
'''
replace_once("src/core/core_ir.c", old_call_verify, new_call_verify)

old_call_dump = r'''        for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
             ++argument_index) {
            MinicCoreValueId argument;

            argument =
                function->call_arguments[instruction->value.call.argument_begin + argument_index];
            if ((argument_index != 0U && fprintf(output, ", ") < 0) ||
                fprintf(output, "%%%" PRIu32, argument) < 0) {
                return false;
            }
        }
'''
new_call_dump = r'''        for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument;

            argument =
                &function->call_arguments[instruction->value.call.argument_begin + argument_index];
            if (argument_index != 0U && fprintf(output, ", ") < 0) {
                return false;
            }
            if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                if (fprintf(output, "%%%" PRIu32, argument->value.value_id) < 0) {
                    return false;
                }
            } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                if (fprintf(output, "object %%o%" PRIu32, argument->value.object_id) < 0) {
                    return false;
                }
            } else {
                return false;
            }
        }
'''
replace_once("src/core/core_ir.c", old_call_dump, new_call_dump)

# Empty volatile asm with read-only inputs and a memory clobber is a compiler
# barrier.  Inputs still remain explicit Core dependencies/evaluations.
replace_once(
    "src/core/core_ir.c",
    "            (inline_asm->template_length == 0U &&\n"
    "             (inline_asm->operand_count != 0U || !inline_asm->has_memory_clobber))) {\n",
    "            (inline_asm->template_length == 0U &&\n"
    "             (inline_asm->output_count != 0U || !inline_asm->has_memory_clobber))) {\n",
)

# ---------------------------------------------------------------------------
# Core lowering: array-object subscript and aggregate direct-call arguments.
# ---------------------------------------------------------------------------
replace_once(
    "src/core/core_lower.c",
    "        if (!core_memory_scalar_type(global->type)) {\n"
    "            return MINIC_CORE_LOWER_UNSUPPORTED;\n"
    "        }\n",
    "        if (!core_memory_scalar_type(global->type) && !minic_type_is_array(global->type)) {\n"
    "            return MINIC_CORE_LOWER_UNSUPPORTED;\n"
    "        }\n",
)

subscript_start = "    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {\n"
subscript_end = "    if (expression->kind == MINIC_EXPRESSION_MEMBER) {\n"
subscript_replacement = r'''    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT) {
        const MinicExpression *base;
        const MinicExpression *index;
        MinicCoreInstruction offset_instruction;
        MinicCoreObjectId base_object;
        MinicCoreValueId base_value;
        MinicCoreValueId index_value;
        MinicCoreLowerStatus subscript_status;
        MinicType element_type;
        MinicType pointer_type;
        MinicArrayObjectInfo array_info;
        bool base_is_array_object;
        size_t element_size;

        base =
            minic_c0_program_expression(context->body->program, expression->value.subscript.base);
        index =
            minic_c0_program_expression(context->body->program, expression->value.subscript.index);
        if (base == NULL || index == NULL || !minic_type_is_integer(index->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        base_is_array_object =
            minic_c0_expression_array_object_info(context->body->program, base, &array_info);
        if (base_is_array_object) {
            element_type = array_info.element_type;
            if (!minic_type_equal(element_type, expression->type) ||
                !minic_type_pointer_to(element_type, &pointer_type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          minic_default_data_layout(),
                                                          pointer_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            subscript_status =
                lower_address(context, expression->value.subscript.base, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
            subscript_status = append_scalar_bitcast(
                context, base->span, pointer_type, base_value, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
        } else {
            pointer_type = base->type;
            if (!minic_type_is_pointer(pointer_type) ||
                !minic_type_pointee(pointer_type, &element_type) ||
                !minic_type_equal(element_type, expression->type) ||
                !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                          minic_default_data_layout(),
                                                          pointer_type,
                                                          &element_size)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            subscript_status =
                lower_expression(context, expression->value.subscript.base, &base_value);
            if (subscript_status != MINIC_CORE_LOWER_OK) {
                return subscript_status;
            }
        }
        if (base_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[base_value].type, pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        subscript_status =
            spill_scalar_value(context, base->span, pointer_type, base_value, &base_object);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }
        subscript_status =
            lower_expression(context, expression->value.subscript.index, &index_value);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }
        if (index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[index_value].type, index->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        subscript_status =
            reload_scalar_value(context, base->span, pointer_type, base_object, &base_value);
        if (subscript_status != MINIC_CORE_LOWER_OK) {
            return subscript_status;
        }

        (void)memset(&offset_instruction, 0, sizeof(offset_instruction));
        offset_instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        offset_instruction.span = expression->span;
        offset_instruction.type = pointer_type;
        offset_instruction.result = MINIC_CORE_VALUE_INVALID;
        offset_instruction.value.pointer_offset.base = base_value;
        offset_instruction.value.pointer_offset.index = index_value;
        offset_instruction.value.pointer_offset.element_size = element_size;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &offset_instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
'''
replace_between("src/core/core_lower.c", subscript_start, subscript_end, subscript_replacement)

call_start = "static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,\n"
call_end = "static MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,\n"
call_replacement = r'''static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,
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
        MinicType parameter_type;
        const MinicExpression *argument_expression;

        parameter_type = callee->parameter_types[argument_index];
        argument_expression = minic_c0_program_expression(
            context->body->program, expression->value.call.arguments[argument_index]);
        if (argument_expression == NULL) {
            free(arguments);
            return MINIC_CORE_LOWER_ERROR;
        }
        if (core_memory_scalar_type(parameter_type)) {
            MinicCoreValueId argument_value;

            status = lower_scalar_assignment_value(context,
                                                   parameter_type,
                                                   expression->value.call.arguments[argument_index],
                                                   &argument_value);
            if (status != MINIC_CORE_LOWER_OK) {
                free(arguments);
                return status;
            }
            if (argument_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[argument_value].type, parameter_type)) {
                free(arguments);
                return MINIC_CORE_LOWER_ERROR;
            }
            status = spill_scalar_value(context,
                                        expression->span,
                                        parameter_type,
                                        argument_value,
                                        &argument_objects[argument_index]);
            if (status != MINIC_CORE_LOWER_OK) {
                free(arguments);
                return status;
            }
            arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
            arguments[argument_index].value.value_id = argument_value;
        } else if (minic_type_is_record(parameter_type)) {
            MinicCoreObjectId object_id;

            if (argument_expression->value_category != MINIC_VALUE_LVALUE ||
                argument_expression->kind != MINIC_EXPRESSION_LOCAL ||
                !minic_type_equal(argument_expression->type, parameter_type)) {
                free(arguments);
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_local_object(context, argument_expression->value.local_id, &object_id);
            if (status != MINIC_CORE_LOWER_OK) {
                free(arguments);
                return status;
            }
            if (object_id >= context->function->object_count ||
                !minic_type_equal(context->function->objects[object_id].type, parameter_type)) {
                free(arguments);
                return MINIC_CORE_LOWER_ERROR;
            }
            arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_OBJECT;
            arguments[argument_index].value.object_id = object_id;
            argument_objects[argument_index] = MINIC_CORE_OBJECT_INVALID;
        } else {
            free(arguments);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        MinicCoreValueId argument_value;

        if (arguments[argument_index].kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
            continue;
        }
        status = reload_scalar_value(context,
                                     expression->span,
                                     callee->parameter_types[argument_index],
                                     argument_objects[argument_index],
                                     &argument_value);
        if (status != MINIC_CORE_LOWER_OK) {
            free(arguments);
            return status;
        }
        arguments[argument_index].value.value_id = argument_value;
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
replace_between("src/core/core_lower.c", call_start, call_end, call_replacement)

replace_once(
    "src/core/core_lower.c",
    "        (source->template_length == 0U &&\n"
    "         (source->output_count != 0U || source->input_count != 0U || !source->has_memory_clobber))) {\n",
    "        (source->template_length == 0U &&\n"
    "         (source->output_count != 0U || !source->has_memory_clobber))) {\n",
)

# ---------------------------------------------------------------------------
# RV64: use the canonical ABI cursor for every direct-call formal.  Small
# integer-only aggregates are loaded from their Core object into their ABI
# chunks; scalar positions therefore remain correct around aggregates.
# ---------------------------------------------------------------------------
replace_once(
    "src/target/riscv64/core_codegen.c",
    "        (inline_asm->template_length == 0U &&\n"
    "         (inline_asm->operand_count != 0U || !inline_asm->has_memory_clobber)) ||\n",
    "        (inline_asm->template_length == 0U &&\n"
    "         (inline_asm->output_count != 0U || !inline_asm->has_memory_clobber)) ||\n",
)

call_support_marker = "static bool core_instruction_supported(const MinicC0Program *program,\n"
call_support = r'''static bool core_call_supported(const MinicC0Program *program,
                                const MinicCoreFunction *function,
                                const MinicCoreInstruction *instruction) {
    const MinicCoreCallee *callee;
    MinicRiscv64AbiCursor cursor;
    MinicRiscv64AbiValue return_value;
    size_t argument_index;

    if (program == NULL || function == NULL || instruction == NULL ||
        instruction->kind != MINIC_CORE_INSTRUCTION_CALL ||
        instruction->value.call.callee_id >= function->callee_count ||
        instruction->value.call.argument_begin > function->call_argument_count ||
        instruction->value.call.argument_count >
            function->call_argument_count - instruction->value.call.argument_begin) {
        return false;
    }
    callee = &function->callees[instruction->value.call.callee_id];
    if (callee->name == NULL || callee->name_length == 0U ||
        instruction->value.call.argument_count != callee->parameter_count ||
        !minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value) ||
        (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
         return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER)) {
        return false;
    }
    for (argument_index = 0U; argument_index < callee->parameter_count; ++argument_index) {
        const MinicCoreCallArgument *argument;
        MinicRiscv64AbiArgumentLocation location;
        MinicType parameter_type;

        parameter_type = callee->parameter_types[argument_index];
        argument = &function->call_arguments[instruction->value.call.argument_begin + argument_index];
        if (!minic_riscv64_abi_place_argument(
                program, parameter_type, true, &cursor, &location) ||
            location.stack_slot_count != 0U) {
            return false;
        }
        if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INTEGER) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                argument->value.value_id >= function->value_count ||
                location.integer_register_count != 1U || location.integer_register_begin >= 8U ||
                !minic_type_equal(function->values[argument->value.value_id].type,
                                  parameter_type)) {
                return false;
            }
        } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT ||
                argument->value.object_id >= function->object_count ||
                location.value.slot_count == 0U || location.value.slot_count > 2U ||
                location.integer_register_count != location.value.slot_count ||
                location.integer_register_begin + location.integer_register_count > 8U ||
                !minic_type_equal(function->objects[argument->value.object_id].type,
                                  parameter_type)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

'''
replace_once(
    "src/target/riscv64/core_codegen.c",
    call_support_marker,
    call_support + call_support_marker,
)

old_switch_call = r'''    case MINIC_CORE_INSTRUCTION_CALL:
        if (instruction->value.call.callee_id >= function->callee_count ||
            instruction->value.call.argument_count > 8U) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        return callee->name != NULL && callee->name_length != 0U && callee->parameter_count <= 8U;
'''
new_switch_call = r'''    case MINIC_CORE_INSTRUCTION_CALL:
        if (program == NULL) {
            if (instruction->value.call.callee_id >= function->callee_count ||
                instruction->value.call.argument_count > 8U) {
                return false;
            }
            callee = &function->callees[instruction->value.call.callee_id];
            return callee->name != NULL && callee->name_length != 0U &&
                   callee->parameter_count <= 8U;
        }
        return core_call_supported(program, function, instruction);
'''
replace_once("src/target/riscv64/core_codegen.c", old_switch_call, new_switch_call)

emit_call_start = "static bool emit_call(FILE *file,\n"
emit_call_end = "static bool emit_field_address(FILE *file,\n"
emit_call_replacement = r'''static bool emit_call(FILE *file,
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
        !core_call_supported(program, function, instruction)) {
        return false;
    }
    callee = &function->callees[instruction->value.call.callee_id];
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value)) {
        return false;
    }
    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
         ++argument_index) {
        const MinicCoreCallArgument *argument;
        MinicRiscv64AbiArgumentLocation location;
        MinicType parameter_type;

        parameter_type = callee->parameter_types[argument_index];
        argument = &function->call_arguments[instruction->value.call.argument_begin + argument_index];
        if (!minic_riscv64_abi_place_argument(
                program, parameter_type, true, &cursor, &location)) {
            return false;
        }
        if (location.value.kind == MINIC_RISCV64_ABI_VALUE_INTEGER) {
            const char *destination_register;

            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                location.integer_register_count != 1U || location.integer_register_begin >= 8U) {
                return false;
            }
            destination_register = minic_core_rv64_argument_registers[location.integer_register_begin];
            if (!load_core_value(file, frame, argument->value.value_id, destination_register)) {
                return false;
            }
        } else if (location.value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
            size_t chunk_index;
            size_t object_offset;

            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_OBJECT ||
                !core_object_offset(program, function, argument->value.object_id, &object_offset) ||
                !emit_sp_address(file, "t0", object_offset)) {
                return false;
            }
            for (chunk_index = 0U; chunk_index < location.value.slot_count; ++chunk_index) {
                size_t register_index;

                register_index = location.integer_register_begin + chunk_index;
                if (register_index >= 8U ||
                    !minic_riscv64_emit_integer_aggregate_load_chunk(
                        file,
                        program,
                        parameter_type,
                        chunk_index,
                        minic_core_rv64_argument_registers[register_index],
                        "t0")) {
                    return false;
                }
            }
        } else {
            return false;
        }
    }
    if (fprintf(file, "  call %s\n", callee->name) < 0) {
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
replace_between(
    "src/target/riscv64/core_codegen.c", emit_call_start, emit_call_end, emit_call_replacement
)

print("M31B_PATCH_APPLIED")
