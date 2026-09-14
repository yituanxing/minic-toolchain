#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
fn_anchor = '''static bool core_cfg_constant_inline_call(const MinicCoreLowerContext *context,
'''
fn_start = text.find(fn_anchor)
fn_end = text.find("\nstatic bool core_const_eval_integer_with_locals", fn_start)
if fn_start < 0 or fn_end < 0:
    raise SystemExit("constant-call leading-if function boundary missing")
body_text = text[fn_start:fn_end]
old = '''    body = minic_c0_program_block(program, callee->body_block);
    if (body == NULL || body->statement_count == 0U) {
        return false;
    }
'''
new = '''    body = minic_c0_program_block(program, callee->body_block);
    if (body == NULL || body->statement_count == 0U) {
        return false;
    }
    /* A leading compile-time if can make the function result independent of
       all later statements (common for CONFIG-disabled helpers). Follow only a
       selected one-return arm; otherwise fall back to the established strict
       direct-return evaluator below. */
    statement = minic_c0_program_statement(program, body->statements[0]);
    if (statement != NULL && statement->kind == MINIC_STATEMENT_IF &&
        statement->expression != MINIC_EXPRESSION_INVALID &&
        statement->cleanup_context == statement->cleanup_stop_context) {
        MinicConstValue condition;
        bool condition_is_zero;
        if (core_cfg_eval_callee_return_expression(context,
                                                   callee,
                                                   expression,
                                                   statement->expression,
                                                   &condition) &&
            minic_const_value_is_zero(program,
                                      context->target,
                                      &condition,
                                      &condition_is_zero)) {
            MinicBlockId selected_id = condition_is_zero
                                           ? statement->else_block
                                           : statement->then_block;
            if (selected_id != MINIC_BLOCK_INVALID) {
                const MinicBlock *selected = minic_c0_program_block(program, selected_id);
                if (selected != NULL && selected->statement_count == 1U) {
                    const MinicStatement *selected_statement = minic_c0_program_statement(
                        program, selected->statements[0]);
                    MinicConstValue selected_value;
                    if (selected_statement != NULL &&
                        selected_statement->kind == MINIC_STATEMENT_RETURN &&
                        selected_statement->expression != MINIC_EXPRESSION_INVALID &&
                        selected_statement->cleanup_context ==
                            selected_statement->cleanup_stop_context &&
                        core_cfg_eval_callee_return_expression(
                            context,
                            callee,
                            expression,
                            selected_statement->expression,
                            &selected_value) &&
                        minic_const_value_convert_integer(program,
                                                          context->target,
                                                          &selected_value,
                                                          expression->type,
                                                          value)) {
                        return true;
                    }
                }
            }
        }
    }
'''
count = body_text.count(old)
if count != 1:
    raise SystemExit(f"constant-call leading-if anchor: expected one, found {count}")
body_text = body_text.replace(old, new, 1)
text = text[:fn_start] + body_text + text[fn_end:]
p.write_text(text)
print("MINIC_CONSTANT_CALL_LEADING_IF_V0=APPLIED")
