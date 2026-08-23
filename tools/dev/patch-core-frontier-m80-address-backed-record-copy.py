#!/usr/bin/env python3
"""Lower address-backed record initialization/copy through Core IR."""

from pathlib import Path

MARKER = "M80_ADDRESS_BACKED_RECORD_COPY"
IR = Path("src/core/core_ir.h")
IR_IMPL = Path("src/core/core_ir.c")
LOWER = Path("src/core/core_lower.c")
CODEGEN = Path("src/target/riscv64/core_codegen.c")


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M80 {name} anchor count={count}")
    return text.replace(old, new, 1)


def patch_ir() -> None:
    text = IR.read_text()
    if MARKER in text:
        print("M80 core_ir.h already applied")
        return
    text = replace_once(
        text,
        '''    MINIC_CORE_INSTRUCTION_LOAD,\n    MINIC_CORE_INSTRUCTION_STORE,\n    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,\n''',
        '''    MINIC_CORE_INSTRUCTION_LOAD,\n    MINIC_CORE_INSTRUCTION_STORE,\n    /* M80_ADDRESS_BACKED_RECORD_COPY: byte-preserving aggregate memory copy. */\n    MINIC_CORE_INSTRUCTION_RECORD_COPY,\n    MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM,\n''',
        "ir-kind",
    )
    text = replace_once(
        text,
        '''        struct {\n            MinicCoreValueId address;\n            MinicCoreValueId stored_value;\n            bool is_volatile;\n        } store;\n        MinicCoreInlineAsmId inline_asm_id;\n''',
        '''        struct {\n            MinicCoreValueId address;\n            MinicCoreValueId stored_value;\n            bool is_volatile;\n        } store;\n        struct {\n            MinicCoreValueId destination_address;\n            MinicCoreValueId source_address;\n        } record_copy;\n        MinicCoreInlineAsmId inline_asm_id;\n''',
        "ir-payload",
    )
    IR.write_text(text)
    print("M80 core_ir.h applied")


def patch_ir_impl() -> None:
    text = IR_IMPL.read_text()
    if MARKER in text:
        print("M80 core_ir.c already applied")
        return
    valid_anchor = '''    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {\n'''
    valid = '''    /* M80_ADDRESS_BACKED_RECORD_COPY: both SSA operands are addresses to the\n       same unqualified record type; legality of writing a const-qualified\n       destination is already established by the frontend initializer/copy node. */\n    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {\n        MinicType destination_pointee;\n        MinicType destination_type;\n        MinicType source_pointee;\n        MinicType source_type;\n        MinicType record_type;\n\n        return instruction->result == MINIC_CORE_VALUE_INVALID &&\n               minic_type_is_record(instruction->type) &&\n               minic_type_unqualified(instruction->type, &record_type) &&\n               minic_type_equal(record_type, instruction->type) &&\n               available_pointer_pointee(function,\n                                         available_values,\n                                         instruction->value.record_copy.destination_address,\n                                         &destination_pointee) &&\n               available_pointer_pointee(function,\n                                         available_values,\n                                         instruction->value.record_copy.source_address,\n                                         &source_pointee) &&\n               minic_type_unqualified(destination_pointee, &destination_type) &&\n               minic_type_unqualified(source_pointee, &source_type) &&\n               minic_type_equal(destination_type, instruction->type) &&\n               minic_type_equal(source_type, instruction->type);\n    }\n    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {\n'''
    text = replace_once(text, valid_anchor, valid, "ir-valid")

    dump_anchor = '''    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {\n'''
    dump = '''    case MINIC_CORE_INSTRUCTION_RECORD_COPY:\n        return fprintf(output,\n                       "  record.copy %%%" PRIu32 ", %%%" PRIu32 "\\n",\n                       instruction->value.record_copy.source_address,\n                       instruction->value.record_copy.destination_address) >= 0;\n    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM: {\n'''
    # The same lexical anchor occurs in verifier and dumper; target the dumper occurrence
    # by replacing only the second occurrence after the store dump.
    store_dump = '''    case MINIC_CORE_INSTRUCTION_STORE:\n        return fprintf(output,\n                       "  store%s %%%" PRIu32 ", %%%" PRIu32 "\\n",\n                       instruction->value.store.is_volatile ? ".volatile" : "",\n                       instruction->value.store.stored_value,\n                       instruction->value.store.address) >= 0;\n'''
    if text.count(store_dump) != 1:
        raise SystemExit(f"M80 ir-dump store anchor count={text.count(store_dump)}")
    pos = text.index(store_dump) + len(store_dump)
    if not text.startswith(dump_anchor, pos):
        raise SystemExit("M80 ir-dump opaque anchor mismatch")
    text = text[:pos] + dump + text[pos + len(dump_anchor):]
    IR_IMPL.write_text(text)
    print("M80 core_ir.c applied")


def patch_lower() -> None:
    text = LOWER.read_text()
    if MARKER in text:
        print("M80 core_lower.c already applied")
        return

    helper_anchor = '''static MinicCoreLowerStatus lower_integer_assignment_value(MinicCoreLowerContext *context,\n'''
    helper = '''/* M80_ADDRESS_BACKED_RECORD_COPY: aggregate values stay address-backed in\n   Core. Resolve the subset whose storage already exists: record lvalues,\n   lvalue-read wrappers, and GNU statement expressions whose final record value\n   is itself address-backed. Calls/conditionals remain fail-closed. */\nstatic MinicCoreLowerStatus lower_record_value_address(MinicCoreLowerContext *context,\n                                                       MinicExpressionId expression_id,\n                                                       MinicCoreValueId *address_id) {\n    const MinicExpression *expression;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL ||\n        address_id == NULL) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    expression = minic_c0_program_expression(context->body->program, expression_id);\n    if (expression == NULL || !minic_type_is_record(expression->type) ||\n        !minic_c0_record_value_is_address_backed(context->body->program, expression_id)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    if (expression->value_category == MINIC_VALUE_LVALUE) {\n        return lower_address(context, expression_id, address_id);\n    }\n    if (expression->value_category != MINIC_VALUE_RVALUE) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {\n        const MinicExpression *operand = minic_c0_program_expression(\n            context->body->program, expression->value.unary.operand);\n        if (operand == NULL || !minic_type_is_record(operand->type) ||\n            operand->type.record_id != expression->type.record_id) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        return lower_record_value_address(context, expression->value.unary.operand, address_id);\n    }\n    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {\n        const MinicBlock *block;\n        const MinicExpression *result;\n        MinicCoreLowerStatus status;\n        bool terminated;\n\n        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        block = minic_c0_program_block(\n            context->body->program, expression->value.statement_expression.block);\n        result = minic_c0_program_expression(\n            context->body->program, expression->value.statement_expression.result);\n        if (block == NULL || result == NULL || !minic_type_is_record(result->type) ||\n            result->type.record_id != expression->type.record_id) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        status = lower_block(context, block, &terminated);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (terminated) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        return lower_record_value_address(\n            context, expression->value.statement_expression.result, address_id);\n    }\n    return MINIC_CORE_LOWER_UNSUPPORTED;\n}\n\nstatic MinicCoreLowerStatus lower_record_copy_statement(MinicCoreLowerContext *context,\n                                                        const MinicStatement *statement) {\n    const MinicExpression *source;\n    const MinicExpression *target;\n    const MinicRecord *record;\n    MinicCoreInstruction instruction;\n    MinicCoreLowerStatus status;\n    MinicCoreValueId destination_address;\n    MinicCoreValueId source_address;\n    MinicType source_type;\n    MinicType target_type;\n\n    if (context == NULL || context->body == NULL || context->body->program == NULL ||\n        context->function == NULL || statement == NULL ||\n        (statement->kind != MINIC_STATEMENT_RECORD_COPY &&\n         statement->kind != MINIC_STATEMENT_RECORD_INITIALIZE)) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    target = minic_c0_program_expression(context->body->program, statement->target_expression);\n    source = minic_c0_program_expression(context->body->program, statement->expression);\n    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||\n        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||\n        target->type.record_id != source->type.record_id ||\n        !minic_type_unqualified(target->type, &target_type) ||\n        !minic_type_unqualified(source->type, &source_type) ||\n        !minic_type_equal(target_type, source_type) || !minic_type_is_record(target_type) ||\n        (statement->kind == MINIC_STATEMENT_RECORD_COPY && minic_type_is_const(target->type)) ||\n        !minic_c0_record_value_is_copy_source(context->body->program, statement->expression) ||\n        !minic_c0_record_value_is_address_backed(context->body->program, statement->expression)) {\n        return MINIC_CORE_LOWER_UNSUPPORTED;\n    }\n    record = minic_c0_program_record(context->body->program, target_type.record_id);\n    if (record == NULL || !record->is_complete) {\n        return MINIC_CORE_LOWER_ERROR;\n    }\n    status = lower_record_value_address(context, statement->expression, &source_address);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    status = lower_address(context, statement->target_expression, &destination_address);\n    if (status != MINIC_CORE_LOWER_OK) {\n        return status;\n    }\n    (void)memset(&instruction, 0, sizeof(instruction));\n    instruction.kind = MINIC_CORE_INSTRUCTION_RECORD_COPY;\n    instruction.span = statement->span;\n    instruction.type = target_type;\n    instruction.result = MINIC_CORE_VALUE_INVALID;\n    instruction.value.record_copy.destination_address = destination_address;\n    instruction.value.record_copy.source_address = source_address;\n    return minic_core_function_append_effect_instruction(\n               context->function, context->block_id, &instruction)\n               ? MINIC_CORE_LOWER_OK\n               : MINIC_CORE_LOWER_ERROR;\n}\n\nstatic MinicCoreLowerStatus lower_integer_assignment_value(MinicCoreLowerContext *context,\n'''
    text = replace_once(text, helper_anchor, helper, "lower-helper")

    switch_anchor = '''            case MINIC_STATEMENT_ASSIGN:\n                status = lower_assignment(context, statement);\n                break;\n            case MINIC_STATEMENT_EXPRESSION:\n'''
    switch_replacement = '''            case MINIC_STATEMENT_ASSIGN:\n                status = lower_assignment(context, statement);\n                break;\n            case MINIC_STATEMENT_RECORD_COPY:\n            case MINIC_STATEMENT_RECORD_INITIALIZE:\n                status = lower_record_copy_statement(context, statement);\n                break;\n            case MINIC_STATEMENT_EXPRESSION:\n'''
    count = text.count(switch_anchor)
    if count < 1:
        raise SystemExit("M80 lower-block switch anchor missing")
    text = text.replace(switch_anchor, switch_replacement)
    LOWER.write_text(text)
    print(f"M80 core_lower.c applied lower_block_sites={count}")


def patch_codegen() -> None:
    text = CODEGEN.read_text()
    if MARKER in text:
        print("M80 core_codegen.c already applied")
        return

    helper_anchor = '''static bool core_call_frame_address_supported(\n'''
    helper = '''static bool core_record_copy_supported(const MinicC0Program *program,\n                                       const MinicCoreFunction *function,\n                                       const MinicCoreInstruction *instruction) {\n    MinicType destination_pointee;\n    MinicType destination_type;\n    MinicType source_pointee;\n    MinicType source_type;\n    size_t alignment;\n    size_t size;\n\n    if (program == NULL || function == NULL || instruction == NULL ||\n        instruction->kind != MINIC_CORE_INSTRUCTION_RECORD_COPY ||\n        instruction->result != MINIC_CORE_VALUE_INVALID || !minic_type_is_record(instruction->type) ||\n        instruction->value.record_copy.destination_address >= function->value_count ||\n        instruction->value.record_copy.source_address >= function->value_count ||\n        !minic_type_pointee(\n            function->values[instruction->value.record_copy.destination_address].type,\n            &destination_pointee) ||\n        !minic_type_pointee(function->values[instruction->value.record_copy.source_address].type,\n                            &source_pointee) ||\n        !minic_type_unqualified(destination_pointee, &destination_type) ||\n        !minic_type_unqualified(source_pointee, &source_type) ||\n        !minic_type_equal(destination_type, instruction->type) ||\n        !minic_type_equal(source_type, instruction->type) ||\n        !minic_data_layout_type(\n            minic_default_data_layout(), program, instruction->type, &size, &alignment)) {\n        return false;\n    }\n    return size != 0U && alignment != 0U;\n}\n\nstatic bool core_call_frame_address_supported(\n'''
    text = replace_once(text, helper_anchor, helper, "codegen-helper")

    support_anchor = '''    case MINIC_CORE_INSTRUCTION_LOAD:\n    case MINIC_CORE_INSTRUCTION_STORE:\n        return true;\n'''
    support_replacement = '''    case MINIC_CORE_INSTRUCTION_LOAD:\n    case MINIC_CORE_INSTRUCTION_STORE:\n        return true;\n    case MINIC_CORE_INSTRUCTION_RECORD_COPY:\n        return core_record_copy_supported(program, function, instruction);\n'''
    text = replace_once(text, support_anchor, support_replacement, "codegen-support")

    emit_anchor = '''    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:\n        return emit_opaque_inline_asm(file, function, symbol_name, instruction);\n'''
    emit = '''    case MINIC_CORE_INSTRUCTION_RECORD_COPY: {\n        size_t alignment;\n        size_t copied;\n        size_t copy_size;\n\n        if (!core_record_copy_supported(program, function, instruction) ||\n            !minic_data_layout_type(minic_default_data_layout(),\n                                    program,\n                                    instruction->type,\n                                    &copy_size,\n                                    &alignment) ||\n            !load_core_value(\n                file, frame, instruction->value.record_copy.destination_address, "t0") ||\n            !load_core_value(file, frame, instruction->value.record_copy.source_address, "t1")) {\n            return false;\n        }\n        (void)alignment;\n        copied = 0U;\n        while (copied < copy_size) {\n            size_t chunk = copy_size - copied;\n            size_t offset;\n            if (chunk > 2048U) {\n                chunk = 2048U;\n            }\n            for (offset = 0U; offset < chunk; ++offset) {\n                if (fprintf(file,\n                            "  lbu t2, %zu(t1)\\n"\n                            "  sb t2, %zu(t0)\\n",\n                            offset,\n                            offset) < 0) {\n                    return false;\n                }\n            }\n            copied += chunk;\n            if (copied < copy_size &&\n                fprintf(file,\n                        "  li t3, %zu\\n"\n                        "  add t0, t0, t3\\n"\n                        "  add t1, t1, t3\\n",\n                        chunk) < 0) {\n                return false;\n            }\n        }\n        return true;\n    }\n    case MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM:\n        return emit_opaque_inline_asm(file, function, symbol_name, instruction);\n'''
    text = replace_once(text, emit_anchor, emit, "codegen-emit")
    CODEGEN.write_text(text)
    print("M80 core_codegen.c applied")


def main() -> int:
    patch_ir()
    patch_ir_impl()
    patch_lower()
    patch_codegen()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
