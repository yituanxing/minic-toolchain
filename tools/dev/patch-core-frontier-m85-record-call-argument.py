#!/usr/bin/env python3
# Add first-class address-backed record arguments to direct Core calls.

from pathlib import Path

MARKER = "M85_RECORD_CALL_ARGUMENT"
IR = Path("src/core/core_ir.h")
IR_IMPL = Path("src/core/core_ir.c")
LOWER = Path("src/core/core_lower.c")
CODEGEN = Path("src/target/riscv64/core_codegen.c")


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M85 {name} anchor count={count}")
    return text.replace(old, new, 1)


def replace_function(text: str, start: str, end: str, replacement: str, name: str) -> str:
    begin = text.find(start)
    if begin < 0:
        raise SystemExit(f"M85 {name} start not found")
    finish = text.find(end, begin + len(start))
    if finish < 0:
        raise SystemExit(f"M85 {name} end not found")
    return text[:begin] + replacement + text[finish:]


def patch_ir() -> None:
    text = IR.read_text()
    if MARKER in text:
        print("M85 core_ir.h already applied")
        return

    signature = '''typedef struct MinicCoreCallSignature {
    MinicFunctionTypeId function_type_id;
    MinicType return_type;
    MinicType *parameter_types;
    size_t parameter_count;
} MinicCoreCallSignature;
'''
    descriptor = signature + '''
/* M85_RECORD_CALL_ARGUMENT: Core call arguments carry semantic storage form,
   not target ABI locations. Scalar arguments are SSA values; aggregate
   by-value arguments are immutable snapshots in Core objects. */
typedef enum MinicCoreCallArgumentKind {
    MINIC_CORE_CALL_ARGUMENT_INVALID = 0,
    MINIC_CORE_CALL_ARGUMENT_VALUE,
    MINIC_CORE_CALL_ARGUMENT_OBJECT
} MinicCoreCallArgumentKind;

typedef struct MinicCoreCallArgument {
    MinicCoreCallArgumentKind kind;
    union {
        MinicCoreValueId value_id;
        MinicCoreObjectId object_id;
    } value;
} MinicCoreCallArgument;
'''
    text = replace_once(text, signature, descriptor, "argument-descriptor")
    text = replace_once(
        text,
        '''    MinicCoreValueId *call_arguments;
''',
        '''    MinicCoreCallArgument *call_arguments;
''',
        "argument-storage",
    )
    text = replace_once(
        text,
        '''bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
                                               const MinicCoreValueId *arguments,
                                               size_t argument_count,
                                               size_t *argument_begin);
''',
        '''bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
                                               const MinicCoreCallArgument *arguments,
                                               size_t argument_count,
                                               size_t *argument_begin);
''',
        "argument-api",
    )
    IR.write_text(text)
    print("M85 core_ir.h applied")


def patch_ir_impl() -> None:
    text = IR_IMPL.read_text()
    if MARKER in text:
        print("M85 core_ir.c already applied")
        return

    scalar_helper = '''static bool core_call_scalar_type(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}
'''
    parameter_helper = scalar_helper + '''
/* M85_RECORD_CALL_ARGUMENT: direct calls may transport address-backed records
   as object snapshots while return values remain on the existing scalar seam. */
static bool core_call_parameter_type(MinicType type) {
    return core_call_scalar_type(type) || minic_type_is_record(type);
}
'''
    text = replace_once(text, scalar_helper, parameter_helper, "parameter-type-helper")
    text = replace_once(
        text,
        '''    for (index = 0U; index < parameter_count; ++index) {
        if (!core_call_scalar_type(parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->inline_asm_count; ++index) {
''',
        '''    for (index = 0U; index < parameter_count; ++index) {
        if (!core_call_parameter_type(parameter_types[index])) {
            return false;
        }
    }
    for (index = 0U; index < function->inline_asm_count; ++index) {
''',
        "callee-parameter-types",
    )
    text = replace_once(
        text,
        '''bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
                                               const MinicCoreValueId *arguments,
                                               size_t argument_count,
                                               size_t *argument_begin) {
''',
        '''bool minic_core_function_append_call_arguments(MinicCoreFunction *function,
                                               const MinicCoreCallArgument *arguments,
                                               size_t argument_count,
                                               size_t *argument_begin) {
''',
        "append-argument-api",
    )

    verify_start = text.find("static bool instruction_is_valid(")
    if verify_start < 0:
        raise SystemExit("M85 verifier function not found")
    call_start = text.find("    case MINIC_CORE_INSTRUCTION_CALL: {", verify_start)
    indirect_start = text.find("    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {", call_start)
    if call_start < 0 or indirect_start < 0:
        raise SystemExit("M85 verifier call cases not found")
    direct_case = '''    case MINIC_CORE_INSTRUCTION_CALL: {
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
        if ((returns_void && instruction->result != MINIC_CORE_VALUE_INVALID) ||
            (!returns_void && !instruction_result_is_valid(function, instruction))) {
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
    text = text[:call_start] + direct_case + text[indirect_start:]

    # Indirect calls remain scalar-only in M85, but consume the shared descriptor array.
    verify_start = text.find("static bool instruction_is_valid(")
    indirect_start = text.find("    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {", verify_start)
    verify_end = text.find("    }\n    return false;\n}\n\nstatic bool terminator_is_valid", indirect_start)
    if indirect_start < 0 or verify_end < 0:
        raise SystemExit("M85 indirect verifier bounds not found")
    old_indirect = text[indirect_start:verify_end]
    old_loop = '''        for (argument_index = instruction->value.indirect_call.argument_begin;
             argument_index < argument_end;
             ++argument_index) {
            MinicCoreValueId argument;
            size_t parameter_index;

            argument = function->call_arguments[argument_index];
            parameter_index =
                argument_index - instruction->value.indirect_call.argument_begin;
            if (argument >= function->value_count || !available_values[argument] ||
                !minic_type_equal(function->values[argument].type,
                                  signature->parameter_types[parameter_index])) {
                return false;
            }
        }
'''
    new_loop = '''        for (argument_index = instruction->value.indirect_call.argument_begin;
             argument_index < argument_end;
             ++argument_index) {
            const MinicCoreCallArgument *argument;
            MinicCoreValueId value_id;
            size_t parameter_index;

            argument = &function->call_arguments[argument_index];
            parameter_index =
                argument_index - instruction->value.indirect_call.argument_begin;
            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE) {
                return false;
            }
            value_id = argument->value.value_id;
            if (value_id >= function->value_count || !available_values[value_id] ||
                !minic_type_equal(function->values[value_id].type,
                                  signature->parameter_types[parameter_index])) {
                return false;
            }
        }
'''
    if old_indirect.count(old_loop) != 1:
        raise SystemExit(f"M85 indirect verifier loop count={old_indirect.count(old_loop)}")
    old_indirect = old_indirect.replace(old_loop, new_loop, 1)
    text = text[:indirect_start] + old_indirect + text[verify_end:]

    dump_start = text.find("static bool dump_instruction(")
    if dump_start < 0:
        raise SystemExit("M85 dump function not found")
    call_start = text.find("    case MINIC_CORE_INSTRUCTION_CALL: {", dump_start)
    indirect_start = text.find("    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {", call_start)
    if call_start < 0 or indirect_start < 0:
        raise SystemExit("M85 dump call cases not found")
    direct_dump = '''    case MINIC_CORE_INSTRUCTION_CALL: {
        const MinicCoreCallee *callee;
        size_t argument_index;

        if (function == NULL || instruction->value.call.callee_id >= function->callee_count) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        if (instruction->result == MINIC_CORE_VALUE_INVALID) {
            if (fprintf(output, "  call @") < 0) {
                return false;
            }
        } else if (fprintf(output, "  %%%" PRIu32 " = call @", instruction->result) < 0) {
            return false;
        }
        if (fwrite(callee->name, 1U, callee->name_length, output) != callee->name_length ||
            fprintf(output, "(") < 0) {
            return false;
        }
        for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument =
                &function->call_arguments[instruction->value.call.argument_begin + argument_index];

            if (argument_index != 0U && fprintf(output, ", ") < 0) {
                return false;
            }
            if (argument->kind == MINIC_CORE_CALL_ARGUMENT_VALUE) {
                if (fprintf(output, "%%%" PRIu32, argument->value.value_id) < 0) {
                    return false;
                }
            } else if (argument->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                if (fprintf(output, "%%o%" PRIu32, argument->value.object_id) < 0) {
                    return false;
                }
            } else {
                return false;
            }
        }
        return fprintf(output, ")\\n") >= 0;
    }
'''
    text = text[:call_start] + direct_dump + text[indirect_start:]

    dump_start = text.find("static bool dump_instruction(")
    indirect_start = text.find("    case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {", dump_start)
    dump_end = text.find("    }\n    return false;\n}\n\nstatic bool dump_terminator", indirect_start)
    if indirect_start < 0 or dump_end < 0:
        raise SystemExit("M85 indirect dump bounds not found")
    old_indirect_dump = text[indirect_start:dump_end]
    old_dump_loop = '''        for (argument_index = 0U;
             argument_index < instruction->value.indirect_call.argument_count;
             ++argument_index) {
            MinicCoreValueId argument;

            argument = function->call_arguments[
                instruction->value.indirect_call.argument_begin + argument_index];
            if ((argument_index != 0U && fprintf(output, ", ") < 0) ||
                fprintf(output, "%%%" PRIu32, argument) < 0) {
                return false;
            }
        }
'''
    new_dump_loop = '''        for (argument_index = 0U;
             argument_index < instruction->value.indirect_call.argument_count;
             ++argument_index) {
            const MinicCoreCallArgument *argument = &function->call_arguments[
                instruction->value.indirect_call.argument_begin + argument_index];

            if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
                (argument_index != 0U && fprintf(output, ", ") < 0) ||
                fprintf(output, "%%%" PRIu32, argument->value.value_id) < 0) {
                return false;
            }
        }
'''
    if old_indirect_dump.count(old_dump_loop) != 1:
        raise SystemExit(f"M85 indirect dump loop count={old_indirect_dump.count(old_dump_loop)}")
    old_indirect_dump = old_indirect_dump.replace(old_dump_loop, new_dump_loop, 1)
    text = text[:indirect_start] + old_indirect_dump + text[dump_end:]

    IR_IMPL.write_text(text)
    print("M85 core_ir.c applied")


def patch_lower() -> None:
    text = LOWER.read_text()
    if MARKER in text:
        print("M85 core_lower.c already applied")
        return

    direct_start = '''static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,
'''
    indirect_marker = '''/* M83_FIRST_CLASS_INDIRECT_CALL: keep the callee as a first-class SSA
'''
    helper_and_direct = r'''/* M85_RECORD_CALL_ARGUMENT: materialize a by-value record argument as a
   private Core object snapshot before later arguments are evaluated. */
static MinicCoreLowerStatus lower_record_call_argument_object(
    MinicCoreLowerContext *context,
    MinicExpressionId expression_id,
    MinicType parameter_type,
    MinicCoreObjectId *object_id) {
    const MinicExpression *expression;
    MinicCoreInstruction instruction;
    MinicCoreValueId destination_address;
    MinicCoreValueId source_address;
    MinicCoreLowerStatus status;
    MinicType source_type;
    MinicType pointer_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || object_id == NULL || !minic_type_is_record(parameter_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type) ||
        !minic_type_unqualified(expression->type, &source_type) ||
        !minic_type_equal(source_type, parameter_type) ||
        !minic_c0_record_value_is_copy_source(context->body->program, expression_id) ||
        !minic_c0_record_value_is_address_backed(context->body->program, expression_id)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_record_value_address(context, expression_id, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!minic_core_function_add_object(context->function, expression->span, parameter_type, object_id) ||
        !minic_type_pointer_to(parameter_type, &pointer_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
    instruction.span = expression->span;
    instruction.type = pointer_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.object_id = *object_id;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &destination_address)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;
    instruction.span = expression->span;
    instruction.type = parameter_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.record_copy.destination_address = destination_address;
    instruction.value.record_copy.source_address = source_address;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,
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
    text = replace_function(text, direct_start, indirect_marker, helper_and_direct, "direct-call")

    indirect_start = '''static MinicCoreLowerStatus lower_indirect_call(MinicCoreLowerContext *context,
'''
    expression_start = '''static MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
'''
    old_begin = text.find(indirect_start)
    old_end = text.find(expression_start, old_begin + 1)
    if old_begin < 0 or old_end < 0:
        raise SystemExit("M85 indirect lower bounds not found")
    indirect_text = text[old_begin:old_end]
    indirect_text = replace_once(
        indirect_text,
        '''    MinicCoreValueId *arguments;
''',
        '''    MinicCoreCallArgument *arguments;
''',
        "indirect-argument-type",
    )
    indirect_text = replace_once(
        indirect_text,
        '''    arguments = signature->parameter_count == 0U
                    ? NULL
                    : (MinicCoreValueId *)malloc(
                          signature->parameter_count * sizeof(*arguments));
''',
        '''    arguments = signature->parameter_count == 0U
                    ? NULL
                    : (MinicCoreCallArgument *)calloc(
                          signature->parameter_count, sizeof(*arguments));
''',
        "indirect-allocation",
    )
    indirect_text = replace_once(
        indirect_text,
        '''        status = lower_scalar_assignment_value(
            context,
            signature->parameter_types[argument_index],
            expression->value.call.arguments[argument_index],
            &arguments[argument_index]);
''',
        '''        arguments[argument_index].kind = MINIC_CORE_CALL_ARGUMENT_VALUE;
        status = lower_scalar_assignment_value(
            context,
            signature->parameter_types[argument_index],
            expression->value.call.arguments[argument_index],
            &arguments[argument_index].value.value_id);
''',
        "indirect-lower",
    )
    indirect_text = replace_once(
        indirect_text,
        '''        if (arguments[argument_index] >= context->function->value_count ||
            !minic_type_equal(context->function->values[arguments[argument_index]].type,
                              signature->parameter_types[argument_index])) {
''',
        '''        if (arguments[argument_index].value.value_id >= context->function->value_count ||
            !minic_type_equal(
                context->function->values[arguments[argument_index].value.value_id].type,
                signature->parameter_types[argument_index])) {
''',
        "indirect-validate",
    )
    indirect_text = replace_once(
        indirect_text,
        '''                                    arguments[argument_index],
                                    &argument_objects[argument_index]);
''',
        '''                                    arguments[argument_index].value.value_id,
                                    &argument_objects[argument_index]);
''',
        "indirect-spill",
    )
    indirect_text = replace_once(
        indirect_text,
        '''                                     &arguments[argument_index]);
''',
        '''                                     &arguments[argument_index].value.value_id);
''',
        "indirect-reload",
    )
    text = text[:old_begin] + indirect_text + text[old_end:]

    LOWER.write_text(text)
    print("M85 core_lower.c applied")


def patch_codegen() -> None:
    text = CODEGEN.read_text()
    if MARKER in text:
        print("M85 core_codegen.c already applied")
        return

    instruction_anchor = '''static bool core_instruction_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    const MinicCoreCallee *callee;
'''
    support_helper = r'''/* M85_RECORD_CALL_ARGUMENT: validate direct-call arguments against the
   shared RV64 ABI classifier. Core stores VALUE/OBJECT form only; register
   placement remains entirely target-owned. The first caller tier intentionally
   stays register-only, matching the pre-M85 Core call backend's no-stack scope. */
static bool core_direct_call_supported(const MinicC0Program *program,
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
        if (callee->parameter_count > 8U) {
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
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value) ||
        (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
         return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER)) {
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

static bool core_instruction_supported(const MinicC0Program *program,
                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    const MinicCoreCallee *callee;
'''
    text = replace_once(text, instruction_anchor, support_helper, "call-support-helper")
    text = replace_once(
        text,
        '''    case MINIC_CORE_INSTRUCTION_CALL:
        if (instruction->value.call.callee_id >= function->callee_count ||
            instruction->value.call.argument_count > 8U) {
            return false;
        }
        callee = &function->callees[instruction->value.call.callee_id];
        return callee->name != NULL && callee->name_length != 0U && callee->parameter_count <= 8U;
''',
        '''    case MINIC_CORE_INSTRUCTION_CALL:
        return core_direct_call_supported(program, function, instruction);
''',
        "call-support-switch",
    )
    # The local callee variable becomes unnecessary after direct-call support moved to the helper.
    text = replace_once(
        text,
        '''                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    const MinicCoreCallee *callee;

    if (function == NULL || instruction == NULL) {
''',
        '''                                       const MinicCoreFunction *function,
                                       const MinicCoreInstruction *instruction) {
    if (function == NULL || instruction == NULL) {
''',
        "remove-unused-callee",
    )

    store_helper = '''static bool
emit_sp_store_chunk(FILE *file, const char *source_register, size_t offset, size_t size) {
'''
    load_helper = r'''static bool
emit_sp_load_chunk(FILE *file, const char *destination_register, size_t offset, size_t size) {
    const char *opcode;
    size_t byte_index;

    if (file == NULL || destination_register == NULL || size == 0U || size > 8U) {
        return false;
    }
    if (size == 8U) {
        return minic_riscv64_emit_sp_load64(file, destination_register, offset);
    }
    opcode = size == 4U ? "lwu" : size == 2U ? "lhu" : size == 1U ? "lbu" : NULL;
    if (opcode != NULL) {
        if (offset <= 2047U) {
            return fprintf(file, "  %s %s, %zu(sp)\n", opcode, destination_register, offset) >= 0;
        }
        return emit_sp_address(file, "t3", offset) &&
               fprintf(file, "  %s %s, 0(t3)\n", opcode, destination_register) >= 0;
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
        if (byte_offset <= 2047U) {
            if (fprintf(file, "  lbu t1, %zu(sp)\n", byte_offset) < 0) {
                return false;
            }
        } else if (!emit_sp_address(file, "t3", byte_offset) ||
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
emit_sp_store_chunk(FILE *file, const char *source_register, size_t offset, size_t size) {
'''
    text = replace_once(text, store_helper, load_helper, "load-chunk-helper")

    call_start = '''static bool emit_call(FILE *file,
'''
    indirect_start = '''static bool emit_indirect_call(FILE *file,
'''
    emit_call = r'''static bool emit_call(FILE *file,
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
        !core_direct_call_supported(program, function, instruction)) {
        return false;
    }
    callee = &function->callees[instruction->value.call.callee_id];
    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value)) {
        return false;
    }
    (void)return_value;
    for (argument_index = 0U; argument_index < instruction->value.call.argument_count;
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
    text = replace_function(text, call_start, indirect_start, emit_call, "emit-call")

    indirect_begin = text.find(indirect_start)
    field_start = text.find("static bool emit_field_address(FILE *file,", indirect_begin)
    if indirect_begin < 0 or field_start < 0:
        raise SystemExit("M85 indirect emitter bounds not found")
    indirect_text = text[indirect_begin:field_start]
    old_load = '''        argument_offset =
            instruction->value.indirect_call.argument_begin + argument_index;
        if (argument_offset >= function->call_argument_count ||
            !load_core_value(file,
                             frame,
                             function->call_arguments[argument_offset],
                             minic_core_rv64_argument_registers[argument_index])) {
            return false;
        }
'''
    new_load = '''        const MinicCoreCallArgument *argument;

        argument_offset =
            instruction->value.indirect_call.argument_begin + argument_index;
        if (argument_offset >= function->call_argument_count) {
            return false;
        }
        argument = &function->call_arguments[argument_offset];
        if (argument->kind != MINIC_CORE_CALL_ARGUMENT_VALUE ||
            !load_core_value(file,
                             frame,
                             argument->value.value_id,
                             minic_core_rv64_argument_registers[argument_index])) {
            return false;
        }
'''
    if indirect_text.count(old_load) != 1:
        raise SystemExit(f"M85 indirect emitter argument count={indirect_text.count(old_load)}")
    indirect_text = indirect_text.replace(old_load, new_load, 1)
    text = text[:indirect_begin] + indirect_text + text[field_start:]

    CODEGEN.write_text(text)
    print("M85 core_codegen.c applied")


def main() -> int:
    patch_ir()
    patch_ir_impl()
    patch_lower()
    patch_codegen()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
