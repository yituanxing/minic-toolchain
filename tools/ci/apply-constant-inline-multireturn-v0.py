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
    {
        bool have_return = false;
        size_t statement_index;

        for (statement_index = 0U; statement_index < body->statement_count; ++statement_index) {
            MinicConstValue converted;

            statement = minic_c0_program_statement(program, body->statements[statement_index]);
            if (statement == NULL || statement->kind != MINIC_STATEMENT_RETURN ||
                statement->expression == MINIC_EXPRESSION_INVALID ||
                statement->cleanup_context != statement->cleanup_stop_context ||
                !minic_const_eval_integer(program,
                                          context->target,
                                          statement->expression,
                                          &returned) ||
                !minic_const_value_convert_integer(program,
                                                   context->target,
                                                   &returned,
                                                   expression->type,
                                                   &converted)) {
                return false;
            }
            if (!have_return) {
                *value = converted;
                have_return = true;
            } else if (converted.bits != value->bits) {
                return false;
            }
        }
        return have_return;
    }
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"constant inline single-return anchor: expected one match, found {count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_CONSTANT_INLINE_MULTIRETURN_V0=APPLIED")
