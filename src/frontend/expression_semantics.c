#include "frontend/expression_semantics.h"

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
