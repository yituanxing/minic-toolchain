#include "frontend/expression_semantics.h"

bool minic_c0_integer_assignment_value_type(const MinicC0Program *program,
                                            MinicType target_type,
                                            MinicExpressionId source_expression_id,
                                            MinicType *result) {
    const MinicExpression *source;

    if (program == NULL || result == NULL || !minic_type_is_integer(target_type)) {
        return false;
    }
    source = minic_c0_program_expression(program, source_expression_id);
    if (source == NULL || !minic_type_is_integer(source->type) ||
        !minic_c0_assignment_compatible(program, target_type, source_expression_id) ||
        !minic_type_unqualified(target_type, result)) {
        return false;
    }
    return minic_type_is_integer(*result);
}

static bool binary_is_integer_comparison(MinicBinaryOperator operator_kind) {
    return operator_kind == MINIC_BINARY_EQUAL || operator_kind == MINIC_BINARY_NOT_EQUAL ||
           operator_kind == MINIC_BINARY_LESS || operator_kind == MINIC_BINARY_LESS_EQUAL ||
           operator_kind == MINIC_BINARY_GREATER || operator_kind == MINIC_BINARY_GREATER_EQUAL;
}

bool minic_c0_integer_comparison_operand_type(const MinicC0Program *program,
                                              const MinicTargetInfo *target,
                                              MinicExpressionId expression_id,
                                              MinicType *result) {
    const MinicExpression *expression;
    const MinicExpression *left;
    const MinicExpression *right;

    if (program == NULL || target == NULL || result == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_BINARY ||
        !binary_is_integer_comparison(expression->value.binary.operator_kind) ||
        !minic_type_equal(expression->type, minic_type_int())) {
        return false;
    }
    left = minic_c0_program_expression(program, expression->value.binary.left);
    right = minic_c0_program_expression(program, expression->value.binary.right);
    return left != NULL && right != NULL && minic_type_is_integer(left->type) &&
           minic_type_is_integer(right->type) &&
           minic_target_info_integer_common(target, left->type, right->type, result);
}

static bool conditional_type_only(const MinicTargetInfo *target,
                                  MinicType when_true,
                                  MinicType when_false,
                                  MinicType *result) {
    bool has_double_operand;
    bool has_numeric_operands;

    if (target == NULL || result == NULL) {
        return false;
    }
    if (minic_type_equal(when_true, when_false)) {
        *result = when_true;
        return true;
    }
    if (minic_type_conditional_pointer_common(when_true, when_false, result)) {
        return true;
    }
    if (minic_type_is_integer(when_true) && minic_type_is_integer(when_false)) {
        return minic_target_info_integer_common(target, when_true, when_false, result);
    }
    has_double_operand = minic_type_is_double(when_true) || minic_type_is_double(when_false);
    has_numeric_operands = (minic_type_is_double(when_true) || minic_type_is_integer(when_true)) &&
                           (minic_type_is_double(when_false) || minic_type_is_integer(when_false));
    if (has_double_operand && has_numeric_operands) {
        *result = minic_type_double();
        return true;
    }
    return false;
}

bool minic_c0_conditional_result_type(const MinicC0Program *program,
                                      const MinicTargetInfo *target,
                                      MinicExpressionId when_true_expression_id,
                                      MinicExpressionId when_false_expression_id,
                                      MinicType *result) {
    const MinicExpression *when_true;
    const MinicExpression *when_false;

    if (program == NULL || target == NULL || result == NULL) {
        return false;
    }
    when_true = minic_c0_program_expression(program, when_true_expression_id);
    when_false = minic_c0_program_expression(program, when_false_expression_id);
    if (when_true == NULL || when_false == NULL) {
        return false;
    }
    if (minic_type_is_pointer(when_true->type) &&
        minic_c0_expression_is_null_pointer_constant_v0(program, when_false_expression_id)) {
        *result = when_true->type;
        return true;
    }
    if (minic_c0_expression_is_null_pointer_constant_v0(program, when_true_expression_id) &&
        minic_type_is_pointer(when_false->type)) {
        *result = when_false->type;
        return true;
    }
    return conditional_type_only(target, when_true->type, when_false->type, result);
}
