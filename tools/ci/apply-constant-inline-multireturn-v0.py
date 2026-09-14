#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
old = '''    body = minic_c0_program_block(program, callee->body_block);
    if (body == NULL || body->statement_count != 1U) {
        return false;
    }
    statement = minic_c0_program_statement(program, body->statements[0]);
    if (statement == NULL || statement->kind != MINIC_STATEMENT_RETURN ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->cleanup_context != statement->cleanup_stop_context ||
        !minic_const_eval_integer(program, context->target, statement->expression, &returned)) {
        return false;
    }
    return minic_const_value_convert_integer(program,
                                             context->target,
                                             &returned,
                                             expression->type,
                                             value);
'''
new = '''    body = minic_c0_program_block(program, callee->body_block);
    if (body == NULL || body->statement_count == 0U) {
        return false;
    }
    /* A top-level return terminates the function unconditionally.  The parser
       may append a synthetic fallback return after it; that statement is
       unreachable and must not be treated as an alternative return value.
       Only consume this narrow shape: the first top-level statement itself is
       a return with no outstanding cleanup.  Conditional/mixed bodies remain
       fail-closed and are handled by the separate CFG-aware helper paths. */
    statement = minic_c0_program_statement(program, body->statements[0]);
    if (statement == NULL || statement->kind != MINIC_STATEMENT_RETURN ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->cleanup_context != statement->cleanup_stop_context ||
        !minic_const_eval_integer(program,
                                  context->target,
                                  statement->expression,
                                  &returned)) {
        return false;
    }
    return minic_const_value_convert_integer(program,
                                             context->target,
                                             &returned,
                                             expression->type,
                                             value);
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"constant inline single-return anchor: expected one match, found {count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_CONSTANT_INLINE_MULTIRETURN_V0=APPLIED top_level_return=1")
