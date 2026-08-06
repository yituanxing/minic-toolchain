#ifndef MINIC_FRONTEND_CAST_NORMALIZATION_H
#define MINIC_FRONTEND_CAST_NORMALIZATION_H

#include "frontend/ast.h"

#include <string.h>

/*
 * Casts remain explicit through parsing and semantic validation.  The current
 * RV64 backend consumes a smaller normalized expression vocabulary, so this
 * single-consumer pass removes only the bounded casts accepted by the parser:
 * pointer-to-pointer casts preserve the operand bit pattern, while integer
 * casts become addition by an integer zero.  RV64 addw supplies the required
 * 32-bit result normalization and the existing unsigned result path performs
 * zero extension when needed.
 */
static bool minic_c0_program_normalize_casts(MinicC0Program *program)
{
    size_t original_expression_count;
    size_t expression_index;

    if (program == NULL) {
        return false;
    }

    original_expression_count = program->expression_count;
    for (expression_index = 0U;
         expression_index < original_expression_count;
         ++expression_index) {
        MinicExpression cast_expression;
        MinicExpression operand_expression;
        MinicExpressionId operand_id;

        if (program->expressions[expression_index].kind !=
            MINIC_EXPRESSION_CAST) {
            continue;
        }

        cast_expression = program->expressions[expression_index];
        operand_id = cast_expression.value.unary.operand;
        if (operand_id >= expression_index ||
            operand_id >= program->expression_count) {
            return false;
        }
        operand_expression = program->expressions[operand_id];

        if (minic_type_is_pointer(cast_expression.type) &&
            minic_type_is_pointer(operand_expression.type)) {
            operand_expression.span = cast_expression.span;
            operand_expression.type = cast_expression.type;
            operand_expression.value_category = MINIC_VALUE_RVALUE;
            program->expressions[expression_index] = operand_expression;
            continue;
        }

        if (minic_type_is_integer(cast_expression.type) &&
            minic_type_is_integer(operand_expression.type)) {
            MinicExpression zero_expression;
            MinicExpression normalized_expression;
            MinicExpressionId zero_id;

            (void)memset(&zero_expression, 0, sizeof(zero_expression));
            zero_expression.kind = MINIC_EXPRESSION_INTEGER;
            zero_expression.span = cast_expression.span;
            zero_expression.type = minic_type_int();
            zero_expression.value_category = MINIC_VALUE_RVALUE;
            zero_expression.value.integer_value = 0;
            if (!minic_c0_program_add_expression(
                    program,
                    &zero_expression,
                    &zero_id)) {
                return false;
            }

            (void)memset(&normalized_expression, 0,
                         sizeof(normalized_expression));
            normalized_expression.kind = MINIC_EXPRESSION_BINARY;
            normalized_expression.span = cast_expression.span;
            normalized_expression.type = cast_expression.type;
            normalized_expression.value_category = MINIC_VALUE_RVALUE;
            normalized_expression.value.binary.operator_kind =
                MINIC_BINARY_ADD;
            normalized_expression.value.binary.left = operand_id;
            normalized_expression.value.binary.right = zero_id;
            program->expressions[expression_index] = normalized_expression;
            continue;
        }

        return false;
    }

    return true;
}

#endif
