#!/usr/bin/env python3
"""Lower discarded comma expressions as ordered effect sequencing."""

from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()

if "M122_DISCARDED_COMMA_EFFECT_SEQUENCE" in text:
    raise SystemExit("M122 already applied")

anchor = '''    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    /* M86B_RECORD_ASSIGNMENT_EXPRESSION_STATEMENT: a record assignment used as
'''
replacement = r'''    if (expression == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    /* M122_DISCARDED_COMMA_EFFECT_SEQUENCE: the C comma operator is an
       explicit sequencing boundary. In expression-statement context its final
       value is discarded, so Core must preserve only the ordered effects:
       evaluate the left operand, then the right operand. Delegate each side
       back through the existing discarded-expression owner rather than forcing
       a scalar SSA result for void/aggregate right operands. Nested comma
       chains naturally recurse through the same path. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicStatement discarded;
        MinicCoreLowerStatus status;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }

        discarded = *statement;
        discarded.kind = MINIC_STATEMENT_EXPRESSION;
        discarded.target_expression = MINIC_EXPRESSION_INVALID;
        discarded.target_statement = MINIC_STATEMENT_INVALID;
        discarded.inline_asm_id = MINIC_INLINE_ASM_INVALID;
        discarded.then_block = MINIC_BLOCK_INVALID;
        discarded.else_block = MINIC_BLOCK_INVALID;
        discarded.expression = expression->value.binary.left;
        discarded.span = left_expression->span;
        status = lower_expression_statement(context, &discarded);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (context->block_id >= context->function->block_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (context->function->blocks[context->block_id].has_terminator) {
            return MINIC_CORE_LOWER_OK;
        }

        discarded.expression = expression->value.binary.right;
        discarded.span = right_expression->span;
        return lower_expression_statement(context, &discarded);
    }
    /* M86B_RECORD_ASSIGNMENT_EXPRESSION_STATEMENT: a record assignment used as
'''

if text.count(anchor) != 1:
    raise SystemExit(f"M122 expression-statement anchor count={text.count(anchor)}")
path.write_text(text.replace(anchor, replacement, 1))
print("M122 discarded comma effect sequencing staged")
