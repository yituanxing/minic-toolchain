#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()

# The C comma operator is an explicit sequencing edge: evaluate the left operand
# for its side effects, discard its value, then evaluate and return the right
# operand. Keep this first slice scalar-result only; aggregate/void results stay
# fail-closed until a concrete consumer requires them.
function_anchor = """static MinicCoreLowerStatus lower_expression(MinicCoreLowerContext *context,
                                             MinicExpressionId expression_id,
                                             MinicCoreValueId *value_id) {
"""
start = text.find(function_anchor)
if start < 0:
    raise SystemExit("M46 lower_expression definition anchor missing")
end = text.find("\nstatic MinicCoreLowerStatus\nlower_condition_branch", start + len(function_anchor))
if end < 0:
    raise SystemExit("M46 lower_expression end boundary missing")
anchor = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND) {
"""
pos = text.find(anchor, start, end)
if pos < 0:
    raise SystemExit("M46 logical-and anchor missing inside lower_expression")
if text.find(anchor, pos + 1, end) >= 0:
    raise SystemExit("M46 logical-and anchor is ambiguous inside lower_expression")

insert = """    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;
        MinicType left_value_type;
        MinicType right_value_type;

        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !core_scalar_expression_value_type(context->body, left_expression, &left_value_type) ||
            !core_scalar_expression_value_type(context->body, right_expression, &right_value_type) ||
            !minic_type_equal(expression->type, right_value_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.binary.left, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (discarded_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[discarded_value].type, left_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, expression->value.binary.right, value_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (*value_id >= context->function->value_count ||
            !minic_type_equal(context->function->values[*value_id].type, right_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return MINIC_CORE_LOWER_OK;
    }
"""
text = text[:pos] + insert + text[pos:]
path.write_text(text)
print("M46_PATCH_APPLIED")
