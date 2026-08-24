#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_ir.c")
text = path.read_text()
old = '''        if (!instruction_is_valid(function, instruction, available_values)) {
            return false;
        }
'''
new = '''        if (!instruction_is_valid(function, instruction, available_values)) {
            (void)fprintf(stderr,
                          "CORE_VERIFY_DETAIL block=%u instruction=%u kind=%d result=%u\\n",
                          (unsigned int)block_id,
                          (unsigned int)instruction_id,
                          (int)instruction->kind,
                          (unsigned int)instruction->result);
            return false;
        }
'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"instruction verifier anchor count={text.count(old)}")
    text = text.replace(old, new, 1)
old = '''    return terminator_is_valid(function, &block->terminator, available_values);
}
'''
new = '''    if (!terminator_is_valid(function, &block->terminator, available_values)) {
        (void)fprintf(stderr,
                      "CORE_VERIFY_DETAIL block=%u terminator=%d condition=%u\\n",
                      (unsigned int)block_id,
                      (int)block->terminator.kind,
                      (unsigned int)block->terminator.conditional.condition);
        return false;
    }
    return true;
}
'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"terminator verifier anchor count={text.count(old)}")
    text = text.replace(old, new, 1)
path.write_text(text)

path = Path("src/core/core_lower.c")
text = path.read_text()
old = '''static MinicCoreLowerStatus lower_assignment_pair(MinicCoreLowerContext *context,
                                                  MinicExpressionId target_id,
                                                  MinicExpressionId source_id,
                                                  MinicSourceSpan span) {
    const MinicExpression *target;
    MinicCoreInstruction instruction;
    MinicCoreObjectId stored_object;
    MinicCoreValueId address_id;
    MinicCoreValueId stored_value;
    MinicCoreLowerStatus status;
    MinicType stored_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target = minic_c0_program_expression(context->body->program, target_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_type_unqualified(target->type, &stored_type) ||
        !core_memory_scalar_type(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = spill_scalar_value(context, span, stored_type, stored_value, &stored_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = lower_address(context, target_id, &address_id);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = reload_scalar_value(context, span, stored_type, stored_object, &stored_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = stored_value;
    instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}
'''
new = '''static MinicCoreLowerStatus lower_assignment_pair(MinicCoreLowerContext *context,
                                                  MinicExpressionId target_id,
                                                  MinicExpressionId source_id,
                                                  MinicSourceSpan span) {
    const MinicExpression *target;
    const MinicExpression *source;
    MinicCoreInstruction instruction;
    MinicCoreObjectId stored_object;
    MinicCoreValueId address_id;
    MinicCoreValueId stored_value;
    MinicCoreLowerStatus status;
    MinicType stored_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    target = minic_c0_program_expression(context->body->program, target_id);
    source = minic_c0_program_expression(context->body->program, source_id);
    if (target == NULL || target->value_category != MINIC_VALUE_LVALUE) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (!minic_type_unqualified(target->type, &stored_type) ||
        !core_memory_scalar_type(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_scalar_assignment_value(context, stored_type, source_id, &stored_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_ASSIGN_STAGE function=%s stage=value status=%d source_kind=%d operand_kind=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status,
                      source != NULL ? (int)source->kind : -1,
                      source != NULL && source->kind == MINIC_EXPRESSION_ADDRESS_OF
                          ? (int)(minic_c0_program_expression(context->body->program,
                                                             source->value.unary.operand) != NULL
                                      ? minic_c0_program_expression(context->body->program,
                                                                    source->value.unary.operand)->kind
                                      : -1)
                          : -1);
        return status;
    }
    status = spill_scalar_value(context, span, stored_type, stored_value, &stored_object);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=spill status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status);
        return status;
    }
    status = lower_address(context, target_id, &address_id);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=target-address status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status);
        return status;
    }
    status = reload_scalar_value(context, span, stored_type, stored_object, &stored_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=reload status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status);
        return status;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address_id;
    instruction.value.store.stored_value = stored_value;
    instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &instruction)) {
        (void)fprintf(stderr, "CORE_ASSIGN_STAGE function=%s stage=store status=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)MINIC_CORE_LOWER_ERROR);
        return MINIC_CORE_LOWER_ERROR;
    }
    return MINIC_CORE_LOWER_OK;
}
'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"assignment-pair trace anchor count={text.count(old)}")
    text = text.replace(old, new, 1)
old = '''    return lower_assignment_pair(context, target_id, source_id, statement->span);
}

static MinicCoreLowerStatus lower_scalar_update'''
new = '''    {
        MinicCoreLowerStatus assignment_status;
        const MinicExpression *target_expression;
        const MinicExpression *source_expression;

        assignment_status =
            lower_assignment_pair(context, target_id, source_id, statement->span);
        if (assignment_status == MINIC_CORE_LOWER_ERROR) {
            target_expression = minic_c0_program_expression(context->body->program, target_id);
            source_expression = minic_c0_program_expression(context->body->program, source_id);
            (void)fprintf(stderr,
                          "CORE_ASSIGN_DETAIL function=%s target_kind=%d source_kind=%d "
                          "target_vc=%d source_vc=%d span=%zu:%zu\\n",
                          context->source_function != NULL ? context->source_function->name : "?",
                          target_expression != NULL ? (int)target_expression->kind : -1,
                          source_expression != NULL ? (int)source_expression->kind : -1,
                          target_expression != NULL ? (int)target_expression->value_category : -1,
                          source_expression != NULL ? (int)source_expression->value_category : -1,
                          statement->span.begin.line,
                          statement->span.begin.column);
        }
        return assignment_status;
    }
}

static MinicCoreLowerStatus lower_scalar_update'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"assignment trace anchor count={text.count(old)}")
    text = text.replace(old, new, 1)
path.write_text(text)
print("CORE_FAST_VERIFY_TRACE_PATCHED")