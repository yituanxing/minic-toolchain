#!/usr/bin/env python3
"""Stage M53: lower effect-only conditional expressions through Core CFG.

C permits `cond ? void_expr_a : void_expr_b`. Linux's bitop() macro uses this
shape for helpers such as __set_bit(). Core already lowers scalar conditionals;
this adds the value-less sibling without inventing a result object.
"""

from pathlib import Path

PATH = Path("src/core/core_lower.c")
MARKER = "M53_VOID_CONDITIONAL_EXPRESSION"


def main() -> int:
    text = PATH.read_text()
    if MARKER in text:
        print("M53 void conditional expression already applied")
        return 0

    anchor = """    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {\n        const MinicExpression *false_expression;\n"""
    if text.count(anchor) != 1:
        raise SystemExit(f"M53 anchor count={text.count(anchor)}")

    block = r'''    /* M53_VOID_CONDITIONAL_EXPRESSION: C permits an effect-only
       conditional when both arms have void type. Model it as CFG only; there is
       deliberately no synthetic scalar result or spill object. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL &&
        !expression->value.conditional.uses_condition_value &&
        expression->value.conditional.when_true != MINIC_EXPRESSION_INVALID &&
        expression->value.conditional.when_false != MINIC_EXPRESSION_INVALID &&
        minic_type_is_void(expression->type)) {
        const MinicExpression *false_expression;
        const MinicExpression *true_expression;
        MinicCoreBlockId false_block;
        MinicCoreBlockId merge_block;
        MinicCoreBlockId true_block;
        MinicCoreValueId discarded_value;
        MinicCoreLowerStatus status;

        true_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_true);
        false_expression = minic_c0_program_expression(
            context->body->program, expression->value.conditional.when_false);
        if (true_expression == NULL || false_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!minic_type_is_void(true_expression->type) ||
            !minic_type_is_void(false_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_core_function_add_block(context->function, &true_block) ||
            !minic_core_function_add_block(context->function, &false_block) ||
            !minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_condition_branch(context,
                                        expression->value.conditional.condition,
                                        expression->span,
                                        true_block,
                                        false_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = true_block;
        status = lower_expression(
            context, expression->value.conditional.when_true, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (discarded_value != MINIC_CORE_VALUE_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = false_block;
        status = lower_expression(
            context, expression->value.conditional.when_false, &discarded_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (discarded_value != MINIC_CORE_VALUE_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_branch(context, context->block_id, expression->span, merge_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = merge_block;
        *value_id = MINIC_CORE_VALUE_INVALID;
        return MINIC_CORE_LOWER_OK;
    }
'''
    PATH.write_text(text.replace(anchor, block + anchor, 1))
    print("M53 void conditional expression applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
