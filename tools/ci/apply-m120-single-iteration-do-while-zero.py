#!/usr/bin/env python3
"""Restore do-while(0) single-iteration lowering with real break/continue ownership."""

from pathlib import Path

p = Path("src/core/core_lower.c")
s = p.read_text()

if "M120_DO_WHILE_ZERO_SINGLE_ITERATION_CFG" in s:
    raise SystemExit("M120 already applied")

helper_anchor = "/* M78_OMITTED_FOR_CONDITION: parse_for represents a missing source\n"
if s.count(helper_anchor) != 1:
    raise SystemExit(f"M120 helper anchor count={s.count(helper_anchor)}")
helper = r'''/* M120_DO_WHILE_ZERO_SINGLE_ITERATION_CFG: parse_do_while lowers source
   `do BODY while (0)` into `while (1) { BODY; continue_label: if (!0) break; }`.
   Recognize that parser-owned tail so Core can lower the construct as one real
   iteration without treating the synthetic label/condition as source control
   flow. The caller binds both source continue and source break to one exit. */
static bool normalized_do_while_zero_body(const MinicCoreLowerContext *context,
                                          const MinicStatement *loop,
                                          const MinicBlock *body,
                                          MinicBlock *single_iteration_body,
                                          MinicStatementId *continue_label_statement) {
    const MinicExpression *loop_condition;
    const MinicExpression *negated_condition;
    const MinicExpression *source_condition;
    const MinicStatement *continue_label;
    const MinicStatement *condition_check;
    const MinicStatement *break_statement;
    const MinicBlock *break_block;
    MinicStatementId continue_id;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || body == NULL || single_iteration_body == NULL ||
        continue_label_statement == NULL || body->statement_count < 2U) {
        return false;
    }
    loop_condition = minic_c0_program_expression(context->body->program, loop->expression);
    continue_id = body->statements[body->statement_count - 2U];
    continue_label = minic_c0_program_statement(context->body->program, continue_id);
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
    source_condition = minic_c0_program_expression(
        context->body->program, negated_condition->value.unary.operand);
    if (source_condition == NULL || source_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(source_condition->type) || source_condition->value.integer_value != 0) {
        return false;
    }
    break_block = minic_c0_program_block(context->body->program, condition_check->then_block);
    if (break_block == NULL || break_block->statement_count != 1U) {
        return false;
    }
    break_statement = minic_c0_program_statement(context->body->program, break_block->statements[0]);
    if (break_statement == NULL || break_statement->kind != MINIC_STATEMENT_BREAK ||
        !core_cleanup_edge_is_empty(break_statement) ||
        !source_position_equal(break_statement->span.begin, loop->span.begin)) {
        return false;
    }

    *single_iteration_body = *body;
    single_iteration_body->statement_count -= 2U;
    *continue_label_statement = continue_id;
    return true;
}

'''
s = s.replace(helper_anchor, helper + helper_anchor, 1)

decl_old = "    MinicBlock normalized_for_body;\n    MinicCoreBlockId body_block;\n"
decl_new = "    MinicBlock normalized_for_body;\n    MinicBlock normalized_do_while_body;\n    MinicStatementId normalized_do_while_continue;\n    MinicCoreBlockId body_block;\n"
if s.count(decl_old) != 1:
    raise SystemExit(f"M120 declaration anchor count={s.count(decl_old)}")
s = s.replace(decl_old, decl_new, 1)

old = '''    /* M119_GENERAL_DO_WHILE_ZERO_CFG: do not flatten normalized do-while(0)
       bodies ahead of the ordinary loop CFG. The old shortcut lowered the body
       before creating the loop exit block and therefore left legitimate break
       statements with MINIC_CORE_BLOCK_INVALID as their target. The standard
       lower_while path already owns condition/body/exit blocks, break routing,
       continue binding and normalized-for tails, so keep one control-flow owner. */
    iteration_source = body_source;
'''
new = r'''    normalized_do_while_continue = MINIC_STATEMENT_INVALID;
    if (normalized_do_while_zero_body(context,
                                      statement,
                                      body_source,
                                      &normalized_do_while_body,
                                      &normalized_do_while_continue)) {
        MinicCoreBlockId single_iteration_exit;

        if (normalized_do_while_continue >= context->statement_block_count ||
            context->statement_blocks == NULL ||
            context->statement_blocks[normalized_do_while_continue] !=
                MINIC_CORE_BLOCK_INVALID ||
            !minic_core_function_add_block(context->function, &single_iteration_exit)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* Source `continue` in do-while(0) means evaluate constant-zero and
           leave the loop. Bind the parser's synthetic continue label directly
           to the real exit before lowering any body goto. */
        context->statement_blocks[normalized_do_while_continue] = single_iteration_exit;
        saved_break_target = context->break_target;
        context->break_target = single_iteration_exit;
        status = lower_block(context, &normalized_do_while_body, &body_terminated);
        context->break_target = saved_break_target;
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=do-while-zero reason=body function=%s "
                          "status=%d span=%zu:%zu\n",
                          context->source_function->name,
                          (int)status,
                          statement->span.begin.line,
                          statement->span.begin.column);
            return status;
        }
        if (!body_terminated) {
            status = set_branch(
                context, context->block_id, statement->span, single_iteration_exit);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        context->block_id = single_iteration_exit;
        *terminated = false;
        return MINIC_CORE_LOWER_OK;
    }

    iteration_source = body_source;
'''
if s.count(old) != 1:
    raise SystemExit(f"M120 M119 replacement anchor count={s.count(old)}")
s = s.replace(old, new, 1)

p.write_text(s)
print("M120 single-iteration do-while-zero CFG staged")
