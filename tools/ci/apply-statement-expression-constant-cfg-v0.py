#!/usr/bin/env python3
from pathlib import Path

# CFG-only constant evaluation for a deliberately tiny GNU statement-expression
# subset: direct integer-local assignments followed by one final pure integer
# expression. Any control flow, non-local write, cleanup, unknown value, or
# unsupported statement fails closed. Runtime lowering is unchanged.
p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M184_STATEMENT_EXPRESSION_CONSTANT_CFG"
if marker in text:
    print("MINIC_STATEMENT_EXPRESSION_CONSTANT_CFG_V0=ALREADY")
    raise SystemExit(0)

fn = "static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"
fn_start = text.find(fn)
if fn_start < 0:
    raise SystemExit("statement-expression local evaluator missing")
fn_end = text.find("\nstatic ", fn_start + len(fn))
if fn_end < 0:
    raise SystemExit("statement-expression local evaluator end missing")
body = text[fn_start:fn_end]
# Later semantic-stack patches may extend the validation prologue. The direct
# local-read branch is the stable first consumer after expression validation,
# so insert immediately before it rather than matching the prologue spelling.
anchor = '''    if (core_direct_local_read(context, expression_id, &local_id) &&
'''
insert = r'''    /* M184_STATEMENT_EXPRESSION_CONSTANT_CFG: interpret only the straight-line
       local-constant subset of GNU ({ ... }) expressions. This is intentionally
       not a general AST interpreter. */
    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
        const MinicBlock *statement_block;
        MinicCoreLowerContext nested_context;
        MinicCoreLocalIntegerConstant *nested_facts = NULL;
        size_t local_count;
        size_t statement_index;
        bool success = false;

        if (context->source_function == NULL ||
            expression->value.statement_expression.block == MINIC_BLOCK_INVALID ||
            expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return false;
        }
        statement_block = minic_c0_program_block(
            context->body->program, expression->value.statement_expression.block);
        if (statement_block == NULL || statement_block->statement_count == 0U) {
            return false;
        }
        local_count = context->source_function->local_count;
        if (local_count != 0U) {
            nested_facts = (MinicCoreLocalIntegerConstant *)calloc(
                local_count, sizeof(*nested_facts));
            if (nested_facts == NULL) {
                return false;
            }
            if (context->local_integer_constants != NULL) {
                (void)memcpy(nested_facts,
                             context->local_integer_constants,
                             local_count * sizeof(*nested_facts));
            }
        }
        nested_context = *context;
        nested_context.local_integer_constants = nested_facts;

        for (statement_index = 0U;
             statement_index < statement_block->statement_count;
             ++statement_index) {
            const MinicStatement *nested_statement = minic_c0_program_statement(
                context->body->program, statement_block->statements[statement_index]);

            if (nested_statement == NULL ||
                nested_statement->cleanup_context != nested_statement->cleanup_stop_context) {
                goto statement_expression_done;
            }
            if (nested_statement->kind == MINIC_STATEMENT_ASSIGN) {
                const MinicExpression *target;
                MinicConstValue assigned;
                MinicConstValue check;

                if (nested_statement->target_expression == MINIC_EXPRESSION_INVALID ||
                    nested_statement->expression == MINIC_EXPRESSION_INVALID) {
                    goto statement_expression_done;
                }
                target = minic_c0_program_expression(
                    context->body->program, nested_statement->target_expression);
                if (target == NULL || target->kind != MINIC_EXPRESSION_LOCAL ||
                    target->value_category != MINIC_VALUE_LVALUE ||
                    !minic_type_is_integer(target->type) ||
                    !core_const_eval_integer_with_locals(
                        &nested_context, nested_statement->expression, &assigned)) {
                    goto statement_expression_done;
                }
                core_local_constant_set(
                    &nested_context, target->value.local_id, &assigned);
                if (!core_local_constant_get(
                        &nested_context, target->value.local_id, &check)) {
                    goto statement_expression_done;
                }
                continue;
            }
            if (nested_statement->kind == MINIC_STATEMENT_EXPRESSION &&
                statement_index + 1U == statement_block->statement_count &&
                nested_statement->expression ==
                    expression->value.statement_expression.result) {
                continue;
            }
            goto statement_expression_done;
        }
        success = core_const_eval_integer_with_locals(
            &nested_context, expression->value.statement_expression.result, value);
statement_expression_done:
        free(nested_facts);
        return success;
    }

'''
count = body.count(anchor)
if count != 1:
    raise SystemExit(f"statement-expression local-read anchor: expected one, found {count}")
body = body.replace(anchor, insert + anchor, 1)
text = text[:fn_start] + body + text[fn_end:]
p.write_text(text)
print("MINIC_STATEMENT_EXPRESSION_CONSTANT_CFG_V0=APPLIED straight_line_locals=1")
