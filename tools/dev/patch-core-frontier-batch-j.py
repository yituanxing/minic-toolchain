#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
old = '''    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type) ||
        !minic_type_unqualified(expression->type, &source_type) ||
        !minic_type_equal(source_type, parameter_type) ||
        !minic_c0_record_value_is_copy_source(context->body->program, expression_id) ||
        !minic_c0_record_value_is_address_backed(context->body->program, expression_id)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_record_value_address(context, expression_id, &source_address);
'''
new = '''    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression == NULL || !minic_type_is_record(expression->type) ||
        !minic_type_unqualified(expression->type, &source_type) ||
        !minic_type_equal(source_type, parameter_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* BATCH_J_DIRECT_RECORD_CALL_ARGUMENT: a direct record-returning call
       already materializes its aggregate result into one private Core object.
       Passing that value immediately by value must reuse that exact result
       object rather than requiring the frontend expression to be pre-classified
       as an address-backed copy source. This composes the existing M86 result
       object seam with the M85 by-value argument seam; no aggregate SSA value
       or target ABI rule is introduced here. */
    if (expression->kind == MINIC_EXPRESSION_CALL &&
        expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
        return lower_direct_record_call_object(context, expression, object_id);
    }
    if (!minic_c0_record_value_is_copy_source(context->body->program, expression_id) ||
        !minic_c0_record_value_is_address_backed(context->body->program, expression_id)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    status = lower_record_value_address(context, expression_id, &source_address);
'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit(f"Batch J record-call argument anchor count={text.count(old)}")
    text = text.replace(old, new, 1)
path.write_text(text)
print("CORE_BATCH_J_PATCHED direct record-call by-value argument")
