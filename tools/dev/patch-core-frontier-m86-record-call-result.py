#!/usr/bin/env python3
# Add direct record-return calls as address-backed Core result objects.

from pathlib import Path

MARKER = "M86_DIRECT_RECORD_CALL_RESULT"
IR = Path("src/core/core_ir.h")
IR_IMPL = Path("src/core/core_ir.c")
LOWER = Path("src/core/core_lower.c")
CODEGEN = Path("src/target/riscv64/core_codegen.c")


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M86 {name} anchor count={count}")
    return text.replace(old, new, 1)


def replace_in_region(text: str, begin: str, end: str, old: str, new: str, name: str) -> str:
    begin_index = text.find(begin)
    if begin_index < 0:
        raise SystemExit(f"M86 {name} region begin missing")
    end_index = text.find(end, begin_index + len(begin))
    if end_index < 0:
        raise SystemExit(f"M86 {name} region end missing")
    region = text[begin_index:end_index]
    count = region.count(old)
    if count != 1:
        raise SystemExit(f"M86 {name} region anchor count={count}")
    region = region.replace(old, new, 1)
    return text[:begin_index] + region + text[end_index:]


def patch_ir() -> None:
    text = IR.read_text()
    if MARKER in text:
        print("M86 core_ir.h already applied")
        return

    old = '''        struct {
            MinicCoreCalleeId callee_id;
            size_t argument_begin;
            size_t argument_count;
        } call;
        struct {
            MinicCoreValueId callee;
'''
    new = '''        struct {
            MinicCoreCalleeId callee_id;
            size_t argument_begin;
            size_t argument_count;
            /* M86_DIRECT_RECORD_CALL_RESULT: aggregate call results remain
               address-backed Core objects rather than becoming aggregate SSA. */
            MinicCoreObjectId result_object;
        } call;
        struct {
            MinicCoreValueId callee;
'''
    text = replace_once(text, old, new, "call-result-object")
    IR.write_text(text)
    print("M86 core_ir.h applied")


def patch_ir_impl() -> None:
    text = IR_IMPL.read_text()
    if MARKER in text:
        print("M86 core_ir.c already applied")
        return

    helper_anchor = '''static bool core_call_parameter_type(MinicType type) {
    return core_call_scalar_type(type) || minic_type_is_record(type);
}
'''
    helper_new = helper_anchor + '''
/* M86_DIRECT_RECORD_CALL_RESULT: direct callees may return an address-backed
   record object. Indirect-call signatures stay on the scalar-return seam. */
static bool core_direct_call_return_type(MinicType type) {
    return minic_type_is_void(type) || core_call_scalar_type(type) || minic_type_is_record(type);
}
'''
    text = replace_once(text, helper_anchor, helper_new, "return-type-helper")

    text = replace_in_region(
        text,
        "bool minic_core_function_add_callee(",
        "/* M83_FIRST_CLASS_INDIRECT_CALL",
        '''        (!minic_type_is_void(return_type) && !core_call_scalar_type(return_type)) ||
''',
        '''        !core_direct_call_return_type(return_type) ||
''',
        "add-callee-return",
    )

    text = replace_in_region(
        text,
        "    for (index = 0U; index < function->callee_count; ++index) {",
        "    for (index = 0U; index < function->call_signature_count; ++index) {",
        '''        if (callee->name == NULL || callee->name_length == 0U ||
            (!minic_type_is_void(callee->return_type) &&
             !core_call_scalar_type(callee->return_type)) ||
            (callee->parameter_count != 0U && callee->parameter_types == NULL)) {
''',
        '''        if (callee->name == NULL || callee->name_length == 0U ||
            !core_direct_call_return_type(callee->return_type) ||
            (callee->parameter_count != 0U && callee->parameter_types == NULL)) {
''',
        "verify-callee-return",
    )

    old_shape = '''        returns_void = minic_type_is_void(callee->return_type);
        if ((returns_void && instruction->result != MINIC_CORE_VALUE_INVALID) ||
            (!returns_void && !instruction_result_is_valid(function, instruction))) {
            return false;
        }
'''
    new_shape = '''        returns_void = minic_type_is_void(callee->return_type);
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
'''
    text = replace_in_region(
        text,
        "static bool instruction_is_valid(",
        "static bool terminator_is_valid(",
        old_shape,
        new_shape,
        "call-result-verify",
    )

    old_dump = '''        callee = &function->callees[instruction->value.call.callee_id];
        if (instruction->result == MINIC_CORE_VALUE_INVALID) {
            if (fprintf(output, "  call @") < 0) {
                return false;
            }
        } else if (fprintf(output, "  %%%" PRIu32 " = call @", instruction->result) < 0) {
            return false;
        }
'''
    new_dump = '''        callee = &function->callees[instruction->value.call.callee_id];
        if (minic_type_is_record(callee->return_type)) {
            if (fprintf(output,
                        "  %%o%" PRIu32 " = call @",
                        instruction->value.call.result_object) < 0) {
                return false;
            }
        } else if (instruction->result == MINIC_CORE_VALUE_INVALID) {
            if (fprintf(output, "  call @") < 0) {
                return false;
            }
        } else if (fprintf(output, "  %%%" PRIu32 " = call @", instruction->result) < 0) {
            return false;
        }
'''
    text = replace_in_region(
        text,
        "static bool dump_instruction(",
        "static bool dump_terminator(",
        old_dump,
        new_dump,
        "dump-record-call",
    )

    IR_IMPL.write_text(text)
    print("M86 core_ir.c applied")


def patch_lower() -> None:
    text = LOWER.read_text()
    if MARKER in text:
        print("M86 core_lower.c already applied")
        return

    declaration_anchor = '''static MinicCoreLowerStatus append_scalar_bitcast(MinicCoreLowerContext *context,
                                                  MinicSourceSpan span,
                                                  MinicType target_type,
                                                  MinicCoreValueId source_value,
                                                  MinicCoreValueId *value_id);
'''
    declaration_new = declaration_anchor + '''static MinicCoreLowerStatus lower_direct_record_call_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object);
'''
    text = replace_once(text, declaration_anchor, declaration_new, "record-call-forward")

    record_guard_old = '''    const MinicRecord *record;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId destination_address;
    MinicCoreValueId source_address;
    MinicType source_type;
    MinicType target_type;
'''
    record_guard_new = '''    const MinicRecord *record;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId destination_address;
    MinicCoreValueId source_address;
    MinicType source_type;
    MinicType target_type;
    bool direct_record_call;
'''
    text = replace_in_region(
        text,
        "static MinicCoreLowerStatus lower_record_copy_statement(",
        "static MinicCoreLowerStatus lower_integer_assignment_value(",
        record_guard_old,
        record_guard_new,
        "record-copy-declarations",
    )

    guard_old = '''    target = minic_c0_program_expression(context->body->program, statement->target_expression);
    source = minic_c0_program_expression(context->body->program, statement->expression);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
        target->type.record_id != source->type.record_id ||
        !minic_type_unqualified(target->type, &target_type) ||
        !minic_type_unqualified(source->type, &source_type) ||
        !minic_type_equal(target_type, source_type) || !minic_type_is_record(target_type) ||
        (statement->kind == MINIC_STATEMENT_RECORD_COPY && minic_type_is_const(target->type)) ||
        !minic_c0_record_value_is_copy_source(context->body->program, statement->expression) ||
        !minic_c0_record_value_is_address_backed(context->body->program, statement->expression)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    guard_new = '''    target = minic_c0_program_expression(context->body->program, statement->target_expression);
    source = minic_c0_program_expression(context->body->program, statement->expression);
    direct_record_call =
        source != NULL && source->kind == MINIC_EXPRESSION_CALL &&
        source->value.call.function_id != MINIC_FUNCTION_INVALID;
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
        target->type.record_id != source->type.record_id ||
        !minic_type_unqualified(target->type, &target_type) ||
        !minic_type_unqualified(source->type, &source_type) ||
        !minic_type_equal(target_type, source_type) || !minic_type_is_record(target_type) ||
        (statement->kind == MINIC_STATEMENT_RECORD_COPY && minic_type_is_const(target->type)) ||
        (!direct_record_call &&
         (!minic_c0_record_value_is_copy_source(context->body->program, statement->expression) ||
          !minic_c0_record_value_is_address_backed(
              context->body->program, statement->expression)))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    text = replace_in_region(
        text,
        "static MinicCoreLowerStatus lower_record_copy_statement(",
        "static MinicCoreLowerStatus lower_integer_assignment_value(",
        guard_old,
        guard_new,
        "record-copy-call-guard",
    )

    source_old = '''    status = lower_record_value_address(context, statement->expression, &source_address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
'''
    source_new = '''    if (direct_record_call) {
        MinicCoreObjectId source_object;
        MinicType pointer_type;

        status = lower_direct_record_call_object(context, source, &source_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (source_object >= context->function->object_count ||
            !minic_type_equal(context->function->objects[source_object].type, source_type) ||
            !minic_type_pointer_to(source_type, &pointer_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = source->span;
        instruction.type = pointer_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = source_object;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &source_address)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    } else {
        status = lower_record_value_address(context, statement->expression, &source_address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
'''
    text = replace_in_region(
        text,
        "static MinicCoreLowerStatus lower_record_copy_statement(",
        "static MinicCoreLowerStatus lower_integer_assignment_value(",
        source_old,
        source_new,
        "record-copy-call-source",
    )

    scalar_payload_old = '''    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = callee->parameter_count;
    if (returns_void) {
'''
    scalar_payload_new = '''    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = callee->parameter_count;
    instruction.value.call.result_object = MINIC_CORE_OBJECT_INVALID;
    if (returns_void) {
'''
    text = replace_once(text, scalar_payload_old, scalar_payload_new, "scalar-call-result-invalid")

    helper_anchor = '''/* M83_FIRST_CLASS_INDIRECT_CALL: keep the callee as a first-class SSA
   function-pointer value; the static signature is copied into Core so
   verification and later backends do not depend on frontend Program state. */
'''
    helper = '''/* M86_DIRECT_RECORD_CALL_RESULT: direct record returns are materialized into
   private Core objects. Arguments keep the M85 VALUE/OBJECT representation and
   the RV64 backend remains the sole owner of ABI register placement. */
static MinicCoreLowerStatus lower_direct_record_call_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object) {
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

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || result_object == NULL ||
        expression->kind != MINIC_EXPRESSION_CALL ||
        expression->value.call.function_id == MINIC_FUNCTION_INVALID ||
        !minic_type_is_record(expression->type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    callee = minic_c0_program_function(context->body->program, expression->value.call.function_id);
    if (callee == NULL || callee->name == NULL || callee->name_length == 0U ||
        !minic_type_is_record(callee->return_type) ||
        !minic_type_equal(callee->return_type, expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    callee_name = callee->assembler_name != NULL ? callee->assembler_name : callee->name;
    callee_name_length =
        callee->assembler_name != NULL ? callee->assembler_name_length : callee->name_length;
    if (callee_name == NULL || callee_name_length == 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (callee->is_variadic || expression->value.call.argument_count != callee->parameter_count) {
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
    if (!minic_core_function_add_object(
            context->function, expression->span, callee->return_type, result_object)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_CALL;
    instruction.span = expression->span;
    instruction.type = callee->return_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.call.callee_id = callee_id;
    instruction.value.call.argument_begin = argument_begin;
    instruction.value.call.argument_count = callee->parameter_count;
    instruction.value.call.result_object = *result_object;
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

'''
    text = replace_once(text, helper_anchor, helper + helper_anchor, "record-call-helper")

    LOWER.write_text(text)
    print("M86 core_lower.c applied")


def patch_codegen() -> None:
    text = CODEGEN.read_text()
    if MARKER in text:
        print("M86 core_codegen.c already applied")
        return

    no_program_old = '''    if (program == NULL) {
        if (callee->parameter_count > 8U) {
'''
    no_program_new = '''    if (program == NULL) {
        if ((!minic_type_is_void(callee->return_type) &&
             !core_scalar_type(callee->return_type)) ||
            callee->parameter_count > 8U) {
'''
    text = replace_in_region(
        text,
        "static bool core_direct_call_supported(",
        "static bool core_instruction_supported(",
        no_program_old,
        no_program_new,
        "codegen-no-program-record-return",
    )

    support_old = '''    if (!minic_riscv64_abi_cursor_initialize_for_return(
            program, callee->return_type, &cursor, &return_value) ||
        (return_value.kind != MINIC_RISCV64_ABI_VALUE_VOID &&
         return_value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER)) {
        return false;
    }
'''
    support_new = '''    /* M86_DIRECT_RECORD_CALL_RESULT: mirror the existing callee-side
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
'''
    text = replace_in_region(
        text,
        "static bool core_direct_call_supported(",
        "static bool core_instruction_supported(",
        support_old,
        support_new,
        "codegen-call-return-support",
    )

    emit_old = '''    if (fprintf(file, "  call %s\\n", callee->name) < 0) {
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
'''
    emit_new = '''    if (fprintf(file, "  call %s\\n", callee->name) < 0) {
        return false;
    }
    if (minic_type_is_void(instruction->type)) {
        return true;
    }
    if (minic_type_is_record(instruction->type)) {
        size_t chunk_index;
        size_t object_offset;

        if (return_value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE ||
            return_value.slot_count == 0U || return_value.slot_count > 2U ||
            instruction->value.call.result_object >= function->object_count ||
            !core_object_offset(
                program, function, instruction->value.call.result_object, &object_offset)) {
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
            if (!emit_sp_store_chunk(
                    file, source_register, object_offset + chunk_offset, chunk_size)) {
                return false;
            }
        }
        return true;
    }
    if (minic_type_is_integer(instruction->type) &&
        !minic_riscv64_emit_integer_conversion_for_program(
            file, program, instruction->type, "a0")) {
        return false;
    }
    return store_core_value(file, frame, instruction->result, "a0");
'''
    text = replace_in_region(
        text,
        "static bool emit_call(FILE *file,",
        "static bool emit_indirect_call(FILE *file,",
        emit_old,
        emit_new,
        "emit-record-return",
    )

    CODEGEN.write_text(text)
    print("M86 core_codegen.c applied")


def main() -> int:
    patch_ir()
    patch_ir_impl()
    patch_lower()
    patch_codegen()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
