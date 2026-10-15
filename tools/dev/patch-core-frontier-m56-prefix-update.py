#!/usr/bin/env python3
"""Stage M56: share Core scalar update lowering between prefix and postfix forms."""

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M56_PREFIX_POSTFIX_SCALAR_UPDATE"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one replacement, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M56 prefix/postfix scalar update already applied")
        return 0

    text = replace_once(
        text,
        "static MinicCoreLowerStatus lower_postfix_scalar_update(MinicCoreLowerContext *context,\n"
        "                                                        const MinicExpression *expression,\n"
        "                                                        MinicCoreValueId *value_id);\n",
        "static MinicCoreLowerStatus lower_scalar_update(MinicCoreLowerContext *context,\n"
        "                                                const MinicExpression *expression,\n"
        "                                                MinicCoreValueId *value_id);\n",
        "prototype",
    )

    old_dispatch = r'''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT)) {
        return lower_postfix_scalar_update(context, expression, value_id);
    }
'''
    new_dispatch = r'''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT)) {
        return lower_scalar_update(context, expression, value_id);
    }
'''
    text = replace_once(text, old_dispatch, new_dispatch, "expression-dispatch")

    old_definition = r'''static MinicCoreLowerStatus lower_postfix_scalar_update(MinicCoreLowerContext *context,
                                                        const MinicExpression *expression,
                                                        MinicCoreValueId *value_id) {
'''
    new_definition = r'''static MinicCoreLowerStatus lower_scalar_update(MinicCoreLowerContext *context,
                                                const MinicExpression *expression,
                                                MinicCoreValueId *value_id) {
'''
    text = replace_once(text, old_definition, new_definition, "definition")

    old_locals = r'''    MinicType stored_type;
    bool increment;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_UNARY ||
        (expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_POST_DECREMENT)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    increment = expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT;
'''
    new_locals = r'''    MinicType stored_type;
    bool increment;
    bool prefix;

    /* M56_PREFIX_POSTFIX_SCALAR_UPDATE: both forms perform the same single
       load/update/store. Only the expression result differs: prefix yields the
       updated value, postfix yields the prior value. */
    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || value_id == NULL ||
        expression->kind != MINIC_EXPRESSION_UNARY ||
        (expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_POST_DECREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_PRE_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_PRE_DECREMENT)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    increment = expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
                expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT;
    prefix = expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT;
'''
    text = replace_once(text, old_locals, new_locals, "operator-semantics")

    text = replace_once(
        text,
        "    *value_id = current;\n    return MINIC_CORE_LOWER_OK;\n}\n\nstatic MinicCoreLowerStatus lower_expression_statement",
        "    *value_id = prefix ? updated : current;\n    return MINIC_CORE_LOWER_OK;\n}\n\nstatic MinicCoreLowerStatus lower_expression_statement",
        "result-selection",
    )

    old_stmt = r'''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT)) {
        MinicCoreValueId discarded_value;

        return lower_postfix_scalar_update(context, expression, &discarded_value);
    }
'''
    new_stmt = r'''    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
         expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT)) {
        MinicCoreValueId discarded_value;

        return lower_scalar_update(context, expression, &discarded_value);
    }
'''
    text = replace_once(text, old_stmt, new_stmt, "statement-dispatch")

    PATH.write_text(text)
    print("M56 prefix/postfix scalar update applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
