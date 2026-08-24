#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
old = '''    if (minic_type_is_void(context->source_function->return_type)) {
        if (statement->expression != MINIC_EXPRESSION_INVALID) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    } else {
'''
new = '''    if (minic_type_is_void(context->source_function->return_type)) {
        if (statement->expression != MINIC_EXPRESSION_INVALID) {
            const MinicExpression *return_expression;
            MinicCoreValueId discarded_value;

            return_expression = minic_c0_program_expression(
                context->body->program, statement->expression);
            if (return_expression == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            /* BATCH_K_GNU_VOID_RETURN_EXPRESSION: Linux uses GNU's
               `return void_call(...);` extension in thin void wrappers.
               The call still has to be evaluated for effects, after which the
               enclosing function returns normally. Keep this narrow to call
               expressions whose semantic type is already void; all other
               value-bearing return forms remain fail-closed. */
            if (!minic_type_is_void(return_expression->type) ||
                return_expression->kind != MINIC_EXPRESSION_CALL) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_expression(context, statement->expression, &discarded_value);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (context->block_id >= context->function->block_count ||
                context->function->blocks[context->block_id].has_terminator) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
    } else {
'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"Batch K GNU void-return anchor count={text.count(old)}")
    text = text.replace(old, new, 1)
path.write_text(text)
print("CORE_BATCH_K_PATCHED GNU void return-expression calls")
