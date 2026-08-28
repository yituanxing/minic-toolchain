#!/usr/bin/env python3
"""Stage M50b: lower effect-only GNU statement expressions through Core."""

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M50B_EFFECT_ONLY_STATEMENT_EXPRESSION"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M50b effect-only statement expression already applied")
        return 0

    old_statement_expr = """    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {\n        const MinicBlock *statement_block;\n        const MinicExpression *statement_result;\n        MinicCoreValueId result_value;\n        MinicCoreLowerStatus status;\n        MinicType result_type;\n        bool terminated;\n\n        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID ||\n            !core_scalar_expression_value_type(context->body, expression, &result_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        statement_block = minic_c0_program_block(context->body->program,\n                                                 expression->value.statement_expression.block);\n        statement_result = minic_c0_program_expression(\n            context->body->program, expression->value.statement_expression.result);\n        if (statement_block == NULL || statement_result == NULL) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        status = lower_block(context, statement_block, &terminated);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (terminated) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        status =\n            lower_expression(context, expression->value.statement_expression.result, &result_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (result_value >= context->function->value_count ||\n            !minic_type_equal(context->function->values[result_value].type, result_type)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        *value_id = result_value;\n        return MINIC_CORE_LOWER_OK;\n    }\n"""
    new_statement_expr = """    if (expression->kind == MINIC_EXPRESSION_STATEMENT) {\n        const MinicBlock *statement_block;\n        const MinicExpression *statement_result;\n        MinicCoreValueId result_value;\n        MinicCoreLowerStatus status;\n        MinicType result_type;\n        bool terminated;\n\n        /* M50B_EFFECT_ONLY_STATEMENT_EXPRESSION: a GNU ({ ... }) whose last\n           statement has no value is an effect expression, not a scalar one. */\n        statement_block = minic_c0_program_block(context->body->program,\n                                                 expression->value.statement_expression.block);\n        if (statement_block == NULL) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {\n            if (!minic_type_is_void(expression->type)) {\n                return MINIC_CORE_LOWER_ERROR;\n            }\n            status = lower_block(context, statement_block, &terminated);\n            if (status != MINIC_CORE_LOWER_OK) {\n                return status;\n            }\n            if (terminated) {\n                return MINIC_CORE_LOWER_UNSUPPORTED;\n            }\n            *value_id = MINIC_CORE_VALUE_INVALID;\n            return MINIC_CORE_LOWER_OK;\n        }\n        if (!core_scalar_expression_value_type(context->body, expression, &result_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        statement_result = minic_c0_program_expression(\n            context->body->program, expression->value.statement_expression.result);\n        if (statement_result == NULL) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        status = lower_block(context, statement_block, &terminated);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (terminated) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        status =\n            lower_expression(context, expression->value.statement_expression.result, &result_value);\n        if (status != MINIC_CORE_LOWER_OK) {\n            return status;\n        }\n        if (result_value >= context->function->value_count ||\n            !minic_type_equal(context->function->values[result_value].type, result_type)) {\n            return MINIC_CORE_LOWER_ERROR;\n        }\n        *value_id = result_value;\n        return MINIC_CORE_LOWER_OK;\n    }\n"""
    text = replace_once(text, old_statement_expr, new_statement_expr, "lower_expression statement expression")

    old_expression_statement = """    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {\n        MinicCoreValueId discarded_value;\n        MinicType discarded_type;\n\n        if (!core_scalar_expression_value_type(context->body, expression, &discarded_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        (void)discarded_type;\n        return lower_expression(context, statement->expression, &discarded_value);\n    }\n"""
    new_expression_statement = """    if (expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {\n        MinicCoreValueId discarded_value;\n        MinicType discarded_type;\n\n        if (expression->kind == MINIC_EXPRESSION_STATEMENT &&\n            expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID &&\n            minic_type_is_void(expression->type)) {\n            return lower_expression(context, statement->expression, &discarded_value);\n        }\n        if (!core_scalar_expression_value_type(context->body, expression, &discarded_type)) {\n            return MINIC_CORE_LOWER_UNSUPPORTED;\n        }\n        (void)discarded_type;\n        return lower_expression(context, statement->expression, &discarded_value);\n    }\n"""
    text = replace_once(text, old_expression_statement, new_expression_statement, "lower_expression_statement void statement expression")

    PATH.write_text(text)
    print("M50b effect-only statement expression applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
