#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


postfix_helper = r'''
static MinicCoreLowerStatus lower_discarded_postfix_integer_increment(
    MinicCoreLowerContext *context, const MinicExpression *expression) {
    const MinicExpression *operand;
    MinicCoreInstruction instruction;
    MinicCoreValueId address;
    MinicCoreValueId current;
    MinicCoreValueId one;
    MinicCoreValueId updated;
    MinicCoreLowerStatus status;
    MinicType stored_type;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL ||
        expression->kind != MINIC_EXPRESSION_UNARY ||
        expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT) {
        return MINIC_CORE_LOWER_ERROR;
    }
    operand =
        minic_c0_program_expression(context->body->program, expression->value.unary.operand);
    if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_integer(operand->type) || minic_type_is_bool_integer(operand->type) ||
        minic_type_is_const(operand->type) ||
        !minic_type_unqualified(operand->type, &stored_type) ||
        !minic_type_equal(expression->type, operand->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_address(context, expression->value.unary.operand, &address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = expression->span;
    instruction.type = stored_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address;
    instruction.value.load.is_volatile = minic_type_is_volatile(operand->type);
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &current)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = expression->span;
    instruction.type = stored_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.integer_value = 1;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &one)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
    instruction.span = expression->span;
    instruction.type = stored_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = current;
    instruction.value.binary.right = one;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &updated)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = expression->span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address;
    instruction.value.store.stored_value = updated;
    instruction.value.store.is_volatile = minic_type_is_volatile(operand->type);
    return minic_core_function_append_effect_instruction(
               context->function, context->block_id, &instruction)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

'''

replace_once(
    "src/core/core_lower.c",
    '''static MinicCoreLowerStatus lower_expression_statement(MinicCoreLowerContext *context,
''',
    postfix_helper + '''static MinicCoreLowerStatus lower_expression_statement(MinicCoreLowerContext *context,
''',
    "Core M20 discarded postfix helper",
)

replace_once(
    "src/core/core_lower.c",
    '''    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        MinicCoreValueId discarded_value;

        return lower_expression(context, statement->expression, &discarded_value);
    }
    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
''',
    '''    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        MinicCoreValueId discarded_value;

        return lower_expression(context, statement->expression, &discarded_value);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT) {
        return lower_discarded_postfix_integer_increment(context, expression);
    }
    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
''',
    "Core M20 discarded postfix routing",
)

for_tail_helper = r'''
static bool normalized_for_update_tail(const MinicCoreLowerContext *context,
                                       const MinicStatement *loop,
                                       const MinicBlock *body,
                                       MinicBlock *iteration_body,
                                       const MinicStatement **update_statement) {
    const MinicStatement *continue_label;
    const MinicStatement *update;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || body == NULL || iteration_body == NULL || update_statement == NULL ||
        body->statement_count < 2U) {
        return false;
    }
    continue_label = minic_c0_program_statement(
        context->body->program, body->statements[body->statement_count - 2U]);
    update = minic_c0_program_statement(
        context->body->program, body->statements[body->statement_count - 1U]);
    if (continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) || update == NULL ||
        update->kind != MINIC_STATEMENT_EXPRESSION ||
        update->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        update->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        update->expression == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    *iteration_body = *body;
    iteration_body->statement_count -= 2U;
    *update_statement = update;
    return true;
}

'''

replace_once(
    "src/core/core_lower.c",
    '''static MinicCoreLowerStatus
lower_while(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
''',
    for_tail_helper + '''static MinicCoreLowerStatus
lower_while(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
''',
    "Core M20 normalized for-tail helper",
)

replace_once(
    "src/core/core_lower.c",
    '''    const MinicBlock *body_source;
    const MinicExpression *condition_expression;
''',
    '''    const MinicBlock *body_source;
    const MinicBlock *iteration_source;
    const MinicExpression *condition_expression;
    const MinicStatement *for_update;
    MinicBlock normalized_for_body;
''',
    "Core M20 loop source declarations",
)

replace_once(
    "src/core/core_lower.c",
    '''    preheader_block = context->block_id;
''',
    '''    iteration_source = body_source;
    for_update = NULL;
    if (normalized_for_update_tail(
            context, statement, body_source, &normalized_for_body, &for_update)) {
        iteration_source = &normalized_for_body;
    }

    preheader_block = context->block_id;
''',
    "Core M20 recognize normalized for tail",
)

replace_once(
    "src/core/core_lower.c",
    '''    context->block_id = body_block;
    status = lower_block(context, body_source, &body_terminated);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!body_terminated) {
        status = set_branch(context, context->block_id, statement->span, condition_block);
''',
    '''    context->block_id = body_block;
    status = lower_block(context, iteration_source, &body_terminated);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!body_terminated && for_update != NULL) {
        status = lower_expression_statement(context, for_update);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    if (!body_terminated) {
        status = set_branch(context, context->block_id, statement->span, condition_block);
''',
    "Core M20 execute normalized for update before backedge",
)

print("staged M20 Core normalized for tail and discarded postfix increment")
