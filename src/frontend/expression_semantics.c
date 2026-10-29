#include "frontend/expression_semantics.h"
#include "frontend/const_eval.h"

bool minic_c0_inline_asm_input_value_supported(
    const MinicExpression *expression, const char *constraint, size_t length) {
    if (expression == NULL || constraint == NULL || length == 0U)
        return false;
    /* Preserve all previously admitted scalar source operands, independent
     * of actual ISA register allocation. */
    if (minic_type_is_integer(expression->type) ||
        minic_type_is_pointer(expression->type))
        return true;
    /* GNU "m" denotes an addressable C storage operand. This includes
     * record/array values (Linux x86 and Loong memcpy fragments) that
     * must not be forced into an integer register or fabricated as a
     * pointer value. The Core/machine must later prove the real address,
     * side effects and instruction template before producing assembly. */
    return length == 1U && constraint[0] == 'm' &&
           expression->value_category == MINIC_VALUE_LVALUE &&
           (minic_type_is_record(expression->type) ||
            minic_type_is_array(expression->type) ||
            minic_type_is_float(expression->type) ||
            minic_type_is_double(expression->type) ||
            minic_type_is_long_double(expression->type));
}

bool minic_c0_inline_asm_output_value_supported(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicExpression *expression, const char *constraint, size_t length) {
    const MinicFixedRegisterBinding *binding;
    if (expression == NULL || constraint == NULL || length == 0U)
        return false;
    if (expression->value_category == MINIC_VALUE_LVALUE)
        return true;
    if (program == NULL || target == NULL ||
        expression->value_category != MINIC_VALUE_RVALUE ||
        expression->kind != MINIC_EXPRESSION_FIXED_REGISTER)
        return false;
    binding = minic_c0_program_fixed_register_binding(
        program, expression->value.fixed_register_binding_id);
    return binding != NULL &&
           minic_target_info_inline_asm_readwrite_hardreg_supported(
               target, binding->register_name, binding->register_name_length,
               constraint, length, expression->type);
}

bool minic_c0_pointer_arithmetic_element_size(const MinicC0Program *program,
                                              const MinicDataLayout *layout,
                                              MinicType pointer_type,
                                              size_t *element_size) {
    MinicType pointee;
    size_t alignment;

    if (program == NULL || layout == NULL || element_size == NULL ||
        !minic_type_pointee(pointer_type, &pointee)) {
        return false;
    }
    /* GNU C gives void* and function-pointer arithmetic a byte stride. */
    if (minic_type_is_void(pointee) || minic_type_is_function(pointee)) {
        *element_size = 1U;
        return true;
    }
    /* M131_ZERO_SIZE_POINTER_STRIDE: GNU complete zero-size object types
       (notably empty records, and the zero-size array forms already accepted by
       the frontend) have a real pointer-arithmetic stride of zero. Completeness
       is owned by the caller/frontend; DataLayout returning a valid size of zero
       must not be reinterpreted here as an unsupported type. Incomplete types
       still fail because DataLayout cannot size them. */
    return minic_data_layout_type(layout, program, pointee, element_size, &alignment);
}

static bool integer_type_is_signed(const MinicC0Program *program, MinicType type) {
    MinicType effective_type;

    return minic_c0_type_effective_integer_type(program, type, &effective_type) &&
           minic_type_is_signed_integer(effective_type);
}

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

static bool conditional_type_only(const MinicC0Program *program,
                                  const MinicTargetInfo *target,
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
    /* GNU C accepts `condition ? void_side_effect() : 0` (and the
     * reversed ordering), giving the entire conditional void type. The
     * nonvoid arm is evaluated solely for side effects; its value is
     * discarded. Linux x86's pgd_clear() uses precisely this extension
     * when the five-level page-table branch invokes native_pgd_clear(). */
    if (minic_type_is_void(when_true) || minic_type_is_void(when_false)) {
        *result = minic_type_void();
        return true;
    }
    if (minic_type_conditional_pointer_common(when_true, when_false, result)) {
        return true;
    }
    if (minic_type_is_integer(when_true) && minic_type_is_integer(when_false)) {
        return minic_target_info_integer_common_for_program(
            target, program, when_true, when_false, result);
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

bool minic_c0_expression_is_null_pointer_constant(
    const MinicC0Program *program, const MinicTargetInfo *target,
    MinicExpressionId expression_id) {
    const MinicExpression *expression;
    MinicType pointee;
    MinicConstValue constant;
    bool is_zero;

    if (program == NULL || target == NULL) return false;
    if (minic_c0_expression_is_null_pointer_constant_v0(program, expression_id))
        return true;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) return false;

    /* A void* cast is a null pointer constant only if its operand is an
     * integer constant expression of value zero. This is the GNU Linux
     * __is_constexpr(x) trick: (void*)((long)(constant) * 0L) must be
     * convertible to the other conditional arm's int* before sizeof(*p).
     * For a runtime x the multiplication is not an ICE, despite always
     * producing zero at runtime, and the conditional keeps void*. */
    if (minic_type_is_pointer(expression->type)) {
        if ((expression->kind != MINIC_EXPRESSION_CAST &&
             expression->kind != MINIC_EXPRESSION_BITCAST) ||
            expression->type.pointer_depth != 1U ||
            !minic_type_pointee(expression->type, &pointee) ||
            !minic_type_is_void(pointee))
            return false;
        expression_id = expression->value.unary.operand;
        expression = minic_c0_program_expression(program, expression_id);
        if (expression == NULL) return false;
    }
    return minic_type_is_integer(expression->type) &&
           minic_const_eval_integer(program, target, expression_id, &constant) &&
           minic_const_value_is_zero(program, target, &constant, &is_zero) &&
           is_zero;
}

bool minic_c0_integer_range_representable_in_type(const MinicC0Program *program,
                                                  const MinicTargetInfo *target,
                                                  MinicType source_type,
                                                  MinicType destination_type) {
    unsigned int source_bits;
    unsigned int destination_bits;
    bool source_signed;
    bool destination_signed;

    if (program == NULL || target == NULL || !minic_type_is_integer(source_type) ||
        !minic_type_is_integer(destination_type) ||
        !minic_target_info_integer_width(target, program, source_type, &source_bits) ||
        !minic_target_info_integer_width(target, program, destination_type, &destination_bits) ||
        source_bits == 0U || destination_bits == 0U) {
        return false;
    }
    if (minic_type_is_bool_integer(source_type)) {
        return destination_bits >= 1U;
    }
    source_signed = integer_type_is_signed(program, source_type);
    destination_signed = integer_type_is_signed(program, destination_type);
    if (source_signed) {
        return destination_signed && destination_bits >= source_bits;
    }
    if (!destination_signed) {
        return destination_bits >= source_bits;
    }
    return destination_bits > source_bits;
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
        minic_c0_expression_is_null_pointer_constant(
            program, target, when_false_expression_id)) {
        *result = when_true->type;
        return true;
    }
    if (minic_c0_expression_is_null_pointer_constant(
            program, target, when_true_expression_id) &&
        minic_type_is_pointer(when_false->type)) {
        *result = when_false->type;
        return true;
    }
    if (minic_type_is_record(when_true->type) && minic_type_is_record(when_false->type) &&
        minic_c0_types_compatible(program, when_true->type, when_false->type)) {
        /* The conditional expression is an rvalue. Keep the common record identity while
         * dropping lvalue-only top-level qualification from either source arm. */
        return minic_type_unqualified(when_true->type, result) && minic_type_is_record(*result);
    }
    return conditional_type_only(program, target, when_true->type, when_false->type, result);
}
