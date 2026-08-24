from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
old = '''        if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
            expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID &&
            minic_type_is_void(expression->type)) {
            const MinicBlock *statement_block;
            bool statement_expression_terminated;

            statement_block = minic_c0_program_block(
                context->body->program, expression->value.statement_expression.block);
            if (statement_block == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            statement_expression_terminated = false;
            return lower_block(context, statement_block, &statement_expression_terminated);
        }
'''
new = '''        if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
            minic_type_is_void(expression->type)) {
            const MinicBlock *statement_block;
            const MinicExpression *statement_result;
            MinicCoreLowerStatus block_status;
            bool statement_expression_terminated;

            /* BATCH_Y_VOID_STATEMENT_EXPRESSION_RESULT: the parser removes a
               GNU statement-expression's final expression from its block and
               stores it as `result`.  Effect-only lowering must therefore run
               both pieces in source order.  A final void call is a real side
               effect even though the enclosing expression has no scalar value. */
            statement_block = minic_c0_program_block(
                context->body->program, expression->value.statement_expression.block);
            if (statement_block == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            statement_expression_terminated = false;
            block_status = lower_block(context, statement_block, &statement_expression_terminated);
            if (block_status != MINIC_CORE_LOWER_OK || statement_expression_terminated ||
                expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
                return block_status;
            }
            statement_result = minic_c0_program_expression(
                context->body->program, expression->value.statement_expression.result);
            if (statement_result == NULL || !minic_type_is_void(statement_result->type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            return lower_expression(context,
                                    expression->value.statement_expression.result,
                                    &discarded_value);
        }
'''
if old not in text:
    if "BATCH_Y_VOID_STATEMENT_EXPRESSION_RESULT" in text:
        print("CORE_BATCH_Y_ALREADY_PATCHED")
        raise SystemExit(0)
    raise SystemExit("Batch Y effect-only statement-expression anchor not found")
path.write_text(text.replace(old, new, 1))
print("CORE_BATCH_Y_PATCHED void statement-expression final result")
