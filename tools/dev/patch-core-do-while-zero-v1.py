from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]
core_path = root / "src/core/core_lower.c"
core = core_path.read_text()

helper = r'''static bool source_position_equal(MinicSourcePosition left, MinicSourcePosition right) {
    return left.offset == right.offset && left.line == right.line && left.column == right.column;
}

static bool normalized_do_while_zero_body(const MinicCoreLowerContext *context,
                                          const MinicStatement *loop,
                                          const MinicBlock *body,
                                          MinicBlock *single_iteration_body) {
    const MinicExpression *loop_condition;
    const MinicExpression *negated_condition;
    const MinicExpression *source_condition;
    const MinicStatement *continue_label;
    const MinicStatement *condition_check;
    const MinicStatement *break_statement;
    const MinicBlock *break_block;

    if (context == NULL || context->body == NULL || context->body->program == NULL || loop == NULL ||
        body == NULL || single_iteration_body == NULL || body->statement_count < 2U) {
        return false;
    }
    loop_condition = minic_c0_program_expression(context->body->program, loop->expression);
    continue_label = minic_c0_program_statement(
        context->body->program, body->statements[body->statement_count - 2U]);
    condition_check = minic_c0_program_statement(
        context->body->program, body->statements[body->statement_count - 1U]);
    if (loop_condition == NULL || loop_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(loop_condition->type) || loop_condition->value.integer_value != 1 ||
        continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) ||
        condition_check == NULL || condition_check->kind != MINIC_STATEMENT_IF ||
        condition_check->expression == MINIC_EXPRESSION_INVALID ||
        condition_check->then_block == MINIC_BLOCK_INVALID ||
        condition_check->else_block != MINIC_BLOCK_INVALID ||
        !source_position_equal(condition_check->span.begin, loop->span.begin)) {
        return false;
    }
    negated_condition =
        minic_c0_program_expression(context->body->program, condition_check->expression);
    if (negated_condition == NULL || negated_condition->kind != MINIC_EXPRESSION_UNARY ||
        negated_condition->value.unary.operator_kind != MINIC_UNARY_LOGICAL_NOT) {
        return false;
    }
    source_condition = minic_c0_program_expression(context->body->program,
                                                   negated_condition->value.unary.operand);
    if (source_condition == NULL || source_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(source_condition->type) || source_condition->value.integer_value != 0) {
        return false;
    }
    break_block = minic_c0_program_block(context->body->program, condition_check->then_block);
    if (break_block == NULL || break_block->statement_count != 1U) {
        return false;
    }
    break_statement =
        minic_c0_program_statement(context->body->program, break_block->statements[0]);
    if (break_statement == NULL || break_statement->kind != MINIC_STATEMENT_BREAK ||
        break_statement->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        break_statement->cleanup_stop_context != MINIC_CLEANUP_CONTEXT_ROOT ||
        !source_position_equal(break_statement->span.begin, loop->span.begin)) {
        return false;
    }

    *single_iteration_body = *body;
    single_iteration_body->statement_count -= 2U;
    return true;
}

'''
core = replace_once(
    core,
    "static MinicCoreLowerStatus\nlower_while(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {\n",
    helper
    + "static MinicCoreLowerStatus\nlower_while(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {\n",
    "insert do-while-zero recognizer",
)

old_validation = r'''    if (condition_expression == NULL || body_source == NULL ||
        !minic_type_is_integer(condition_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    preheader_block = context->block_id;
'''
new_validation = r'''    if (condition_expression == NULL || body_source == NULL ||
        !minic_type_is_integer(condition_expression->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    {
        MinicBlock single_iteration_body;

        if (normalized_do_while_zero_body(context, statement, body_source, &single_iteration_body)) {
            status = lower_block(context, &single_iteration_body, &body_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            *terminated = body_terminated;
            return MINIC_CORE_LOWER_OK;
        }
    }

    preheader_block = context->block_id;
'''
core = replace_once(core, old_validation, new_validation, "route do-while-zero")
core_path.write_text(core)
