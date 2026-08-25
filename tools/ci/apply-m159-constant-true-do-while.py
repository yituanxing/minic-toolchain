#!/usr/bin/env python3
from pathlib import Path
import re

path = Path("src/core/core_lower.c")
text = path.read_text()
marker = "M159_CONSTANT_TRUE_DO_WHILE_REACHABILITY_OWNER"
if marker in text:
    print("M159 constant-true do-while owner already staged")
    raise SystemExit(0)
if "M158_FINAL_STRICT_TAIL" not in Path("src/core/core_ir.h").read_text():
    raise SystemExit("M159 requires staged M158 final-tail semantics")

# Temporary M158 ingress diagnostics were useful only to prove #414 was not a
# parameter-ingress failure.  Remove them before the qualified semantic cut.
text, removed = re.subn(
    r'\n\s*\(void\)fprintf\(stderr,\n\s*"CORE_M158_INGRESS_DETAIL.*?\n\s*\);',
    '',
    text,
    flags=re.S,
)
if removed < 2:
    raise SystemExit(f"M159 expected M158 ingress diagnostics, removed {removed}")

anchor = '''static bool normalized_for_continue_tail(const MinicCoreLowerContext *context,
                                         const MinicStatement *loop,
                                         const MinicBlock *body,
                                         MinicBlock *iteration_body) {
'''
if text.count(anchor) != 1:
    raise SystemExit("M159 could not locate post do-while helper seam")
helper = r'''/* M159_CONSTANT_TRUE_DO_WHILE_REACHABILITY_OWNER: parse_do_while lowers
   every source do/while into an outer synthetic while(1), followed inside its
   body by a synthetic continue label and `if (!source_condition) break`.
   When source_condition is a compile-time nonzero integer and the original
   body contains no break/continue edge that needs those synthetic nodes, that
   tail is unreachable control metadata.  Strip it from the executable view so
   the ordinary constant-true while CFG has no false exit predecessor. */
static bool normalized_do_while_true_body(const MinicCoreLowerContext *context,
                                          const MinicStatement *loop,
                                          const MinicBlock *body,
                                          MinicBlock *iteration_body) {
    const MinicExpression *loop_condition;
    const MinicExpression *negated_condition;
    const MinicExpression *source_condition;
    const MinicStatement *continue_label;
    const MinicStatement *condition_check;
    const MinicStatement *break_statement;
    const MinicBlock *break_block;
    MinicConstValue condition_value;
    MinicStatementId continue_id;
    bool is_zero;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || loop == NULL || body == NULL || iteration_body == NULL ||
        body->statement_count < 2U) {
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
    if (source_condition == NULL || !minic_type_is_integer(source_condition->type) ||
        !minic_const_eval_integer(context->body->program,
                                 context->target,
                                 negated_condition->value.unary.operand,
                                 &condition_value) ||
        !minic_const_value_is_zero(context->body->program,
                                   context->target,
                                   &condition_value,
                                   &is_zero) ||
        is_zero) {
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

    *iteration_body = *body;
    iteration_body->statement_count -= 2U;
    return !normalized_do_while_block_needs_exit(
        context, iteration_body, continue_id, true);
}

'''
text = text.replace(anchor, helper + anchor, 1)

anchor = '''    } else if (normalized_for_continue_tail(
                   context, statement, body_source, &normalized_for_body)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    }
'''
if text.count(anchor) != 1:
    raise SystemExit("M159 could not locate lower_while normalized-tail seam")
replacement = anchor + r'''    if (!normalized_for &&
        normalized_do_while_true_body(
            context, statement, body_source, &normalized_do_while_body)) {
        iteration_source = &normalized_do_while_body;
    }
'''
text = text.replace(anchor, replacement, 1)
path.write_text(text)

Path("tests/compiler/c0/m159_constant_true_do_while.c").write_text(r'''int m159_infinite_return_loop(int x) {
    do {
        if (x)
            return 1;
        x += 1;
    } while (1);
}

int m159_false_tail_not_stripped(int x) {
    do {
        x += 1;
    } while (0);
    return x;
}
''')
print("staged M159 constant-true do-while reachability owner")
