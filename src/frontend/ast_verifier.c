#include "frontend/ast_verifier.h"
#include "frontend/expression_semantics.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static bool storage_is_valid(const void *data, size_t count, size_t capacity) {
    return count <= capacity && (count == 0U || data != NULL);
}

static bool pointer_qualifiers_are_valid(MinicType type) {
    unsigned int capacity;

    capacity = (unsigned int)(sizeof(type.pointer_qualifiers) * CHAR_BIT);
    if (type.pointer_depth > capacity) {
        return false;
    }
    if (type.pointer_depth == capacity) {
        return true;
    }
    return (type.pointer_qualifiers >> type.pointer_depth) == 0U &&
           (type.pointer_volatile_qualifiers >> type.pointer_depth) == 0U;
}

static bool
type_is_valid(const MinicC0Program *program, const MinicTargetInfo *target, MinicType type) {
    if (program == NULL ||
        (type.base_qualifiers & ~((unsigned int)MINIC_TYPE_QUALIFIER_CONST |
                                  (unsigned int)MINIC_TYPE_QUALIFIER_VOLATILE)) != 0U ||
        !pointer_qualifiers_are_valid(type)) {
        return false;
    }
    if (type.base_kind != MINIC_TYPE_BASE_ENUM && type.enum_id != MINIC_ENUM_INVALID) {
        return false;
    }
    if (type.is_plain_char && type.base_kind != MINIC_TYPE_BASE_INT) {
        return false;
    }
    if (type.is_plain_char) {
        MinicIntegerSign plain_char_sign;

        if (type.integer_rank != MINIC_INTEGER_RANK_CHAR ||
            !minic_target_info_plain_char_sign(target, &plain_char_sign) ||
            type.integer_sign != plain_char_sign) {
            return false;
        }
    }

    switch (type.base_kind) {
    case MINIC_TYPE_BASE_VOID:
        return type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
               type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
               type.integer_rank == MINIC_INTEGER_RANK_NONE;
    case MINIC_TYPE_BASE_INT:
        return type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
               (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
                type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
               (type.integer_rank == MINIC_INTEGER_RANK_BOOL ||
                type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
                type.integer_rank == MINIC_INTEGER_RANK_SHORT ||
                type.integer_rank == MINIC_INTEGER_RANK_INT ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_INT128);
    case MINIC_TYPE_BASE_ENUM: {
        const MinicEnum *entity;

        entity = minic_c0_program_enum(program, type.enum_id);
        return entity != NULL && type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.function_type_id == MINIC_FUNCTION_TYPE_INVALID && !type.is_plain_char &&
               (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
                type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
               (type.integer_rank == MINIC_INTEGER_RANK_INT ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG_LONG) &&
               (!entity->is_complete ||
                (type.integer_sign == entity->compatible_type.integer_sign &&
                 type.integer_rank == entity->compatible_type.integer_rank));
    }
    case MINIC_TYPE_BASE_FLOAT:
    case MINIC_TYPE_BASE_DOUBLE:
        return type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
               type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
               type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char;
    case MINIC_TYPE_BASE_FUNCTION:
        return type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.function_type_id < program->function_type_count &&
               type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
               type.integer_rank == MINIC_INTEGER_RANK_NONE && !type.is_plain_char &&
               type.base_qualifiers == MINIC_TYPE_QUALIFIER_NONE;
    case MINIC_TYPE_BASE_RECORD:
        return type.record_id < program->record_count &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
               type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
               type.integer_rank == MINIC_INTEGER_RANK_NONE;
    case MINIC_TYPE_BASE_ARRAY:
        return type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id < program->array_type_count &&
               type.function_type_id == MINIC_FUNCTION_TYPE_INVALID &&
               type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
               type.integer_rank == MINIC_INTEGER_RANK_NONE;
    }
    return false;
}

static bool type_is_top_level_unqualified(MinicType type) {
    MinicType unqualified;

    return minic_type_unqualified(type, &unqualified) && minic_type_equal(type, unqualified);
}

static const MinicExpression *expression_before(const MinicC0Program *program,
                                                MinicExpressionId expression_id,
                                                size_t parent_index) {
    if (program == NULL || expression_id >= parent_index ||
        expression_id >= program->expression_count) {
        return NULL;
    }
    return &program->expressions[expression_id];
}

static const MinicExpression *program_expression(const MinicC0Program *program,
                                                 MinicExpressionId expression_id) {
    if (program == NULL || expression_id >= program->expression_count) {
        return NULL;
    }
    return &program->expressions[expression_id];
}

static bool binary_is_comparison(MinicBinaryOperator operator_kind) {
    return operator_kind == MINIC_BINARY_EQUAL || operator_kind == MINIC_BINARY_NOT_EQUAL ||
           operator_kind == MINIC_BINARY_LESS || operator_kind == MINIC_BINARY_LESS_EQUAL ||
           operator_kind == MINIC_BINARY_GREATER || operator_kind == MINIC_BINARY_GREATER_EQUAL;
}

static bool binary_is_shift(MinicBinaryOperator operator_kind) {
    return operator_kind == MINIC_BINARY_SHIFT_LEFT || operator_kind == MINIC_BINARY_SHIFT_RIGHT;
}

static bool binary_is_double_arithmetic(MinicBinaryOperator operator_kind) {
    return operator_kind == MINIC_BINARY_ADD || operator_kind == MINIC_BINARY_SUBTRACT ||
           operator_kind == MINIC_BINARY_MULTIPLY || operator_kind == MINIC_BINARY_DIVIDE;
}

static bool binary_is_logical(MinicBinaryOperator operator_kind) {
    return operator_kind == MINIC_BINARY_LOGICAL_AND || operator_kind == MINIC_BINARY_LOGICAL_OR;
}

static bool binary_operator_is_valid(MinicBinaryOperator operator_kind) {
    return operator_kind >= MINIC_BINARY_ADD && operator_kind <= MINIC_BINARY_COMMA;
}

static bool unary_operator_is_valid(MinicUnaryOperator operator_kind) {
    return operator_kind >= MINIC_UNARY_PLUS && operator_kind <= MINIC_UNARY_PRE_DECREMENT;
}

static bool expression_is_integer_zero(const MinicExpression *expression) {
    return expression != NULL && expression->kind == MINIC_EXPRESSION_INTEGER &&
           minic_type_is_integer(expression->type) && expression->value.integer_value == 0;
}

static bool type_is_condition_scalar(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}

static bool verify_binary_type(const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicExpression *expression,
                               const MinicExpression *left,
                               const MinicExpression *right) {
    MinicType expected_type;
    MinicType pointer_type;
    MinicType pointee_type;

    if (!binary_operator_is_valid(expression->value.binary.operator_kind)) {
        return false;
    }

    if (expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
        return minic_type_equal(expression->type, right->type);
    }

    if (binary_is_logical(expression->value.binary.operator_kind)) {
        return type_is_condition_scalar(left->type) && type_is_condition_scalar(right->type) &&
               minic_type_equal(expression->type, minic_type_int());
    }

    if ((expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) &&
        minic_c0_pointer_equality_compatible(
            program, expression->value.binary.left, expression->value.binary.right)) {
        return minic_type_equal(expression->type, minic_type_int());
    }

    if ((expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL) &&
        minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
        minic_c0_pointer_relational_compatible(program, left->type, right->type)) {
        return minic_type_equal(expression->type, minic_type_int());
    }

    if (minic_type_is_integer(left->type) && minic_type_is_integer(right->type)) {
        if (binary_is_comparison(expression->value.binary.operator_kind)) {
            expected_type = minic_type_int();
        } else if (binary_is_shift(expression->value.binary.operator_kind)) {
            if (!minic_target_info_integer_promotion(target, left->type, &expected_type)) {
                return false;
            }
        } else if (!minic_target_info_integer_common(
                       target, left->type, right->type, &expected_type)) {
            return false;
        }
        return minic_type_equal(expression->type, expected_type);
    }

    if (binary_is_comparison(expression->value.binary.operator_kind) &&
        (minic_type_is_double(left->type) || minic_type_is_integer(left->type)) &&
        (minic_type_is_double(right->type) || minic_type_is_integer(right->type)) &&
        (minic_type_is_double(left->type) || minic_type_is_double(right->type))) {
        return minic_type_equal(expression->type, minic_type_int());
    }

    if ((minic_type_is_double(left->type) || minic_type_is_integer(left->type)) &&
        (minic_type_is_double(right->type) || minic_type_is_integer(right->type)) &&
        (minic_type_is_double(left->type) || minic_type_is_double(right->type)) &&
        binary_is_double_arithmetic(expression->value.binary.operator_kind)) {
        return minic_type_is_double(expression->type);
    }

    if (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT &&
        minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type)) {
        return minic_type_equal(expression->type, minic_type_long()) &&
               minic_c0_pointer_relational_compatible(program, left->type, right->type);
    }

    if (expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
        if (minic_type_is_pointer(left->type) && minic_type_is_integer(right->type)) {
            pointer_type = left->type;
        } else if (minic_type_is_integer(left->type) && minic_type_is_pointer(right->type)) {
            pointer_type = right->type;
        } else {
            return false;
        }
    } else if (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT &&
               minic_type_is_pointer(left->type) && minic_type_is_integer(right->type)) {
        pointer_type = left->type;
    } else {
        return false;
    }

    return minic_type_equal(expression->type, pointer_type) &&
           minic_type_pointee(pointer_type, &pointee_type) &&
           minic_c0_pointer_arithmetic_pointee_allowed(program, pointee_type);
}

static bool array_object_type_matches(const MinicC0Program *program,
                                      const MinicArrayObjectInfo *info,
                                      MinicType array_type) {
    const MinicArrayType *materialized;

    if (program == NULL || info == NULL || !minic_type_is_array(array_type) ||
        info->is_zero_length) {
        return false;
    }
    materialized = minic_c0_program_array_type(program, array_type.array_type_id);
    if (materialized == NULL || !minic_type_equal(materialized->element_type, info->element_type)) {
        return false;
    }
    return info->is_incomplete ? materialized->element_count == 0U
                               : materialized->element_count == info->element_count;
}

static bool verify_subscript_type(const MinicC0Program *program,
                                  const MinicExpression *base,
                                  MinicType result_type) {
    MinicArrayObjectInfo array_info;
    MinicType pointee_type;

    if (minic_c0_expression_array_object_info(program, base, &array_info)) {
        return minic_type_equal(array_info.element_type, result_type);
    }
    if (minic_type_pointee(base->type, &pointee_type)) {
        return minic_type_equal(pointee_type, result_type);
    }
    return false;
}

static bool verify_call_arguments(const MinicC0Program *program,
                                  const MinicExpression *expression,
                                  size_t expression_index,
                                  const MinicType *parameter_types,
                                  size_t parameter_count,
                                  bool is_variadic) {
    size_t argument_index;

    if (expression == NULL || parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        expression->value.call.argument_count < parameter_count ||
        expression->value.call.argument_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (!is_variadic && expression->value.call.argument_count != parameter_count)) {
        return false;
    }
    for (argument_index = 0U; argument_index < expression->value.call.argument_count;
         ++argument_index) {
        const MinicExpression *argument;

        argument = expression_before(
            program, expression->value.call.arguments[argument_index], expression_index);
        if (argument == NULL) {
            return false;
        }
        if (argument_index < parameter_count) {
            if (!minic_c0_fixed_call_argument_compatible(
                    program,
                    parameter_types[argument_index],
                    expression->value.call.arguments[argument_index])) {
                return false;
            }
        } else if (!minic_type_is_integer(argument->type) &&
                   !minic_type_is_pointer(argument->type) &&
                   !minic_type_is_double(argument->type)) {
            return false;
        }
    }
    return true;
}

static bool verify_expression(const MinicC0Program *program,
                              size_t expression_index,
                              MinicC0AstForm form,
                              const MinicTargetInfo *target) {
    const MinicExpression *expression;
    const MinicExpression *left;
    const MinicExpression *right;
    const MinicExpression *operand;

    expression = &program->expressions[expression_index];
    if (!type_is_valid(program, target, expression->type) ||
        (expression->value_category != MINIC_VALUE_RVALUE &&
         expression->value_category != MINIC_VALUE_LVALUE)) {
        return false;
    }

    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
        return expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_integer(expression->type);
    case MINIC_EXPRESSION_FLOATING:
        return expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_double(expression->type);
    case MINIC_EXPRESSION_LOCAL: {
        const MinicLocal *local;

        local = minic_c0_program_local(program, expression->value.local_id);
        return local != NULL && expression->value_category == MINIC_VALUE_LVALUE &&
               minic_type_equal(expression->type, local->type);
    }
    case MINIC_EXPRESSION_GLOBAL_OBJECT: {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(program, expression->value.global_object_id);
        return object != NULL && expression->value_category == MINIC_VALUE_LVALUE &&
               minic_type_equal(expression->type, object->type);
    }
    case MINIC_EXPRESSION_FIXED_REGISTER: {
        const MinicFixedRegisterBinding *binding;

        binding = minic_c0_program_fixed_register_binding(
            program, expression->value.fixed_register_binding_id);
        return target != NULL && binding != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, binding->type) &&
               (minic_type_is_integer(expression->type) ||
                minic_type_is_pointer(expression->type)) &&
               minic_target_info_fixed_register_supported(
                   target, binding->register_name, binding->register_name_length);
    }
    case MINIC_EXPRESSION_FUNCTION: {
        const MinicFunction *function;
        const MinicFunctionType *function_type;
        MinicType pointee;
        size_t parameter_index;

        function = minic_c0_program_function(program, expression->value.function_id);
        if (function == NULL || function->is_variadic ||
            expression->value_category != MINIC_VALUE_RVALUE ||
            !minic_type_pointee(expression->type, &pointee) || !minic_type_is_function(pointee)) {
            return false;
        }
        function_type = minic_c0_program_function_type(program, pointee.function_type_id);
        if (function_type == NULL || function_type->parameter_count != function->parameter_count ||
            !minic_type_equal(function_type->return_type, function->return_type)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
            if (!minic_type_equal(function_type->parameter_types[parameter_index],
                                  function->parameter_types[parameter_index])) {
                return false;
            }
        }
        return true;
    }
    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
        return expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_void(expression->type);
    case MINIC_EXPRESSION_CALL_FRAME_ADDRESS: {
        MinicType pointee;

        return expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_pointee(expression->type, &pointee) && minic_type_is_void(pointee) &&
               minic_target_info_call_frame_address_supported(
                   target,
                   expression->value.call_frame_address.kind,
                   expression->value.call_frame_address.level);
    }
    case MINIC_EXPRESSION_LABEL_ADDRESS: {
        const MinicStatement *label;
        MinicType pointee;

        label = minic_c0_program_statement(program, expression->value.label_statement_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_pointee(expression->type, &pointee) && minic_type_is_void(pointee);
    }
    case MINIC_EXPRESSION_SIZEOF: {
        size_t measured_size;

        return target != NULL && expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_unsigned_long()) &&
               type_is_valid(program, target, expression->value.sizeof_type) &&
               minic_target_info_sizeof_type(
                   target, program, expression->value.sizeof_type, &measured_size);
    }
    case MINIC_EXPRESSION_OFFSETOF: {
        const MinicRecord *record;

        record = minic_c0_program_record(program, expression->value.offsetof_value.record_id);
        return record != NULL && record->is_complete &&
               expression->value.offsetof_value.field_index < record->field_count &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_unsigned_long());
    }
    case MINIC_EXPRESSION_ADDRESS_OF: {
        MinicType pointee_type;
        MinicType function_type;

        operand = expression_before(program, expression->value.unary.operand, expression_index);
        if (operand == NULL || expression->value_category != MINIC_VALUE_RVALUE) {
            return false;
        }
        if (operand->kind == MINIC_EXPRESSION_FUNCTION) {
            return minic_type_equal(expression->type, operand->type) &&
                   minic_type_pointee(operand->type, &function_type) &&
                   minic_type_is_function(function_type);
        }
        if (minic_type_is_function(operand->type)) {
            return minic_type_pointee(expression->type, &pointee_type) &&
                   minic_type_equal(pointee_type, operand->type);
        }
        if (operand->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_pointee(expression->type, &pointee_type)) {
            return false;
        }
        {
            MinicArrayObjectInfo array_info;

            if (minic_c0_expression_array_object_info(program, operand, &array_info)) {
                return array_object_type_matches(program, &array_info, pointee_type);
            }
        }
        return minic_type_equal(pointee_type, operand->type);
    }
    case MINIC_EXPRESSION_DEREFERENCE: {
        MinicType pointee_type;

        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return operand != NULL && expression->value_category == MINIC_VALUE_LVALUE &&
               minic_type_pointee(operand->type, &pointee_type) &&
               minic_type_equal(expression->type, pointee_type);
    }
    case MINIC_EXPRESSION_CAST:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_PARSED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               (minic_type_is_void(expression->type) ||
                minic_type_cast_compatible(expression->type, operand->type) ||
                (minic_type_is_pointer(expression->type) && expression_is_integer_zero(operand)));
    case MINIC_EXPRESSION_BITCAST:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_cast_compatible(expression->type, operand->type) &&
               ((minic_type_is_pointer(expression->type) &&
                 (minic_type_is_pointer(operand->type) || minic_type_is_integer(operand->type))) ||
                (minic_type_is_integer(expression->type) && minic_type_is_pointer(operand->type)));
    case MINIC_EXPRESSION_CONVERSION:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               ((minic_type_is_double(expression->type) &&
                 (minic_type_is_integer(operand->type) || minic_type_is_float(operand->type))) ||
                (minic_type_is_integer(expression->type) && minic_type_is_double(operand->type)) ||
                (minic_type_is_integer(expression->type) && minic_type_is_integer(operand->type) &&
                 minic_type_cast_compatible(expression->type, operand->type)));
    case MINIC_EXPRESSION_DISCARD:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_void(expression->type);
    case MINIC_EXPRESSION_SUBSCRIPT:
        left = expression_before(program, expression->value.subscript.base, expression_index);
        right = expression_before(program, expression->value.subscript.index, expression_index);
        return left != NULL && right != NULL && expression->value_category == MINIC_VALUE_LVALUE &&
               minic_type_is_integer(right->type) &&
               verify_subscript_type(program, left, expression->type);
    case MINIC_EXPRESSION_MEMBER: {
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicType record_type;
        MinicType expected_type;
        bool record_rvalue_base;

        operand = expression_before(program, expression->value.member.base, expression_index);
        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (operand == NULL || record == NULL || field == NULL) {
            return false;
        }
        record_rvalue_base = operand->value_category == MINIC_VALUE_RVALUE &&
                             minic_type_is_record(operand->type) &&
                             operand->type.record_id == expression->value.member.record_id;
        if (record_rvalue_base) {
            record_type = operand->type;
            if (!minic_c0_record_value_is_copy_source(program, expression->value.member.base) ||
                field->is_array || minic_type_is_record(field->type) ||
                expression->value_category != MINIC_VALUE_RVALUE) {
                return false;
            }
        } else if (!minic_type_pointee(operand->type, &record_type) ||
                   !minic_type_is_record(record_type) ||
                   record_type.record_id != expression->value.member.record_id ||
                   expression->value_category != MINIC_VALUE_LVALUE) {
            return false;
        }
        expected_type = field->type;
        if (minic_type_is_const(record_type) &&
            !minic_type_add_const(expected_type, &expected_type)) {
            return false;
        }
        return minic_type_equal(expression->type, expected_type);
    }
    case MINIC_EXPRESSION_LVALUE_READ:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return operand != NULL && operand->value_category == MINIC_VALUE_LVALUE &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, operand->type) &&
               (minic_type_is_integer(expression->type) ||
                minic_type_is_pointer(expression->type) || minic_type_is_double(expression->type));
    case MINIC_EXPRESSION_ASSIGNMENT:
        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        if (left == NULL || right == NULL || left->value_category != MINIC_VALUE_LVALUE ||
            minic_type_is_const(left->type) ||
            minic_c0_expression_array_object_info(program, left, NULL) ||
            minic_type_is_function(left->type) ||
            expression->value_category != MINIC_VALUE_RVALUE ||
            !minic_type_equal(expression->type, left->type)) {
            return false;
        }
        if (minic_type_is_record(left->type)) {
            return minic_c0_record_value_is_copy_source(program, expression->value.binary.right) &&
                   minic_type_is_record(right->type) &&
                   left->type.record_id == right->type.record_id;
        }
        return minic_c0_assignment_compatible(program, left->type, expression->value.binary.right);
    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT: {
        MinicType common_type;
        MinicType pointee_type;
        MinicBinaryOperator operator_kind;

        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        operator_kind = expression->value.binary.operator_kind;
        if (left == NULL || right == NULL || left->value_category != MINIC_VALUE_LVALUE ||
            expression->value_category != MINIC_VALUE_RVALUE ||
            !minic_type_equal(expression->type, left->type) || minic_type_is_const(left->type) ||
            minic_c0_expression_array_object_info(program, left, NULL)) {
            return false;
        }
        if (minic_type_is_pointer(left->type)) {
            return (operator_kind == MINIC_BINARY_ADD || operator_kind == MINIC_BINARY_SUBTRACT) &&
                   minic_type_is_integer(right->type) &&
                   minic_type_pointee(left->type, &pointee_type) &&
                   minic_c0_type_is_complete_object(program, pointee_type);
        }
        if (minic_type_is_double(left->type)) {
            return (operator_kind == MINIC_BINARY_ADD || operator_kind == MINIC_BINARY_SUBTRACT ||
                    operator_kind == MINIC_BINARY_MULTIPLY ||
                    operator_kind == MINIC_BINARY_DIVIDE) &&
                   (minic_type_is_double(right->type) || minic_type_is_integer(right->type));
        }
        if (operator_kind != MINIC_BINARY_ADD && operator_kind != MINIC_BINARY_SUBTRACT &&
            operator_kind != MINIC_BINARY_MULTIPLY && operator_kind != MINIC_BINARY_DIVIDE &&
            operator_kind != MINIC_BINARY_REMAINDER && operator_kind != MINIC_BINARY_BITWISE_AND &&
            operator_kind != MINIC_BINARY_BITWISE_OR && operator_kind != MINIC_BINARY_BITWISE_XOR &&
            operator_kind != MINIC_BINARY_SHIFT_LEFT && operator_kind != MINIC_BINARY_SHIFT_RIGHT) {
            return false;
        }
        return minic_type_is_integer(left->type) && minic_type_is_integer(right->type) &&
               minic_target_info_integer_common(target, left->type, right->type, &common_type);
    }
    case MINIC_EXPRESSION_UNARY: {
        MinicType expected_type;
        MinicType pointee_type;

        operand = expression_before(program, expression->value.unary.operand, expression_index);
        if (operand == NULL || expression->value_category != MINIC_VALUE_RVALUE ||
            !unary_operator_is_valid(expression->value.unary.operator_kind)) {
            return false;
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
            expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
            expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
            expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) {
            if (operand->value_category != MINIC_VALUE_LVALUE ||
                minic_type_is_const(operand->type) ||
                !minic_type_equal(expression->type, operand->type)) {
                return false;
            }
            if (minic_type_is_integer(operand->type)) {
                return true;
            }
            return minic_type_is_pointer(operand->type) &&
                   minic_type_pointee(operand->type, &pointee_type) &&
                   minic_c0_type_is_complete_object(program, pointee_type);
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
            return type_is_condition_scalar(operand->type) &&
                   minic_type_equal(expression->type, minic_type_int());
        }
        if ((expression->value.unary.operator_kind == MINIC_UNARY_PLUS ||
             expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) &&
            minic_type_is_double(operand->type)) {
            return minic_type_equal(expression->type, operand->type);
        }
        if (!minic_type_is_integer(operand->type) ||
            !minic_target_info_integer_promotion(target, operand->type, &expected_type)) {
            return false;
        }
        return minic_type_equal(expression->type, expected_type);
    }
    case MINIC_EXPRESSION_BINARY:
        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        return left != NULL && right != NULL && expression->value_category == MINIC_VALUE_RVALUE &&
               verify_binary_type(program, target, expression, left, right);
    case MINIC_EXPRESSION_CONDITIONAL: {
        const MinicExpression *condition;
        const MinicExpression *when_true;
        const MinicExpression *when_false;
        MinicType expected_type;

        condition =
            expression_before(program, expression->value.conditional.condition, expression_index);
        when_true =
            expression_before(program, expression->value.conditional.when_true, expression_index);
        when_false =
            expression_before(program, expression->value.conditional.when_false, expression_index);
        return condition != NULL && when_true != NULL && when_false != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               type_is_condition_scalar(condition->type) &&
               (!expression->value.conditional.uses_condition_value ||
                expression->value.conditional.when_true ==
                    expression->value.conditional.condition) &&
               minic_c0_conditional_result_type(program,
                                                target,
                                                expression->value.conditional.when_true,
                                                expression->value.conditional.when_false,
                                                &expected_type) &&
               minic_type_equal(expression->type, expected_type);
    }
    case MINIC_EXPRESSION_BUILTIN_UNARY: {
        const MinicExpression *builtin_operand;

        builtin_operand =
            expression_before(program, expression->value.builtin_unary.operand, expression_index);
        return expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL &&
               builtin_operand != NULL &&
               minic_type_equal(builtin_operand->type, minic_type_unsigned_long_long()) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_int());
    }
    case MINIC_EXPRESSION_BUILTIN_OVERFLOW: {
        const MinicExpression *overflow_left;
        const MinicExpression *overflow_right;
        const MinicExpression *result_pointer;
        MinicType result_type;

        overflow_left =
            expression_before(program, expression->value.overflow.left, expression_index);
        overflow_right =
            expression_before(program, expression->value.overflow.right, expression_index);
        result_pointer =
            expression_before(program, expression->value.overflow.result_pointer, expression_index);
        return overflow_left != NULL && overflow_right != NULL && result_pointer != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_bool()) &&
               expression->value.overflow.operator_kind >= MINIC_OVERFLOW_ADD &&
               expression->value.overflow.operator_kind <= MINIC_OVERFLOW_MULTIPLY &&
               minic_type_pointee(result_pointer->type, &result_type) &&
               minic_type_is_integer(result_type) && !minic_type_is_bool_integer(result_type) &&
               minic_type_equal(overflow_left->type, result_type) &&
               minic_type_equal(overflow_right->type, result_type);
    }
    case MINIC_EXPRESSION_COMPOUND_LITERAL: {
        const MinicLocal *local;
        const MinicBlock *initializer_block;
        const MinicRecord *record;

        local = minic_c0_program_local(program, expression->value.compound_literal.local_id);
        initializer_block =
            minic_c0_program_block(program, expression->value.compound_literal.initializer_block);
        record = minic_type_is_record(expression->type)
                     ? minic_c0_program_record(program, expression->type.record_id)
                     : NULL;
        return local != NULL && initializer_block != NULL && record != NULL &&
               record->is_complete && expression->value_category == MINIC_VALUE_LVALUE &&
               !local->is_array && !local->is_register_storage && local->element_count == 1U &&
               minic_type_equal(local->type, expression->type);
    }
    case MINIC_EXPRESSION_STATEMENT: {
        const MinicBlock *block;
        const MinicExpression *result;

        block = minic_c0_program_block(program, expression->value.statement_expression.block);
        if (block == NULL || expression->value_category != MINIC_VALUE_RVALUE) {
            return false;
        }
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return minic_type_is_void(expression->type);
        }
        result = expression_before(
            program, expression->value.statement_expression.result, expression_index);
        return result != NULL && minic_type_equal(expression->type, result->type);
    }
    case MINIC_EXPRESSION_CALL:
        if (expression->value_category != MINIC_VALUE_RVALUE) {
            return false;
        }
        if (expression->value.call.function_id != MINIC_FUNCTION_INVALID) {
            const MinicFunction *function;

            function = minic_c0_program_function(program, expression->value.call.function_id);
            return function != NULL && minic_type_equal(expression->type, function->return_type) &&
                   verify_call_arguments(program,
                                         expression,
                                         expression_index,
                                         function->parameter_types,
                                         function->parameter_count,
                                         function->is_variadic);
        } else {
            const MinicExpression *callee;
            const MinicFunctionType *function_type;
            MinicType callee_type;

            callee = expression_before(program, expression->value.call.callee, expression_index);
            if (callee == NULL) {
                return false;
            }
            callee_type = callee->type;
            if (!minic_type_is_function(callee_type) &&
                (!minic_type_pointee(callee->type, &callee_type) ||
                 !minic_type_is_function(callee_type))) {
                return false;
            }
            function_type = minic_c0_program_function_type(program, callee_type.function_type_id);
            return function_type != NULL &&
                   minic_type_equal(expression->type, function_type->return_type) &&
                   verify_call_arguments(program,
                                         expression,
                                         expression_index,
                                         function_type->parameter_types,
                                         function_type->parameter_count,
                                         false);
        }
    }
    return false;
}

static bool verify_statement(const MinicC0Program *program,
                             const MinicTargetInfo *target_info,
                             const MinicStatement *statement) {
    const MinicExpression *target;
    const MinicExpression *expression;

    if (statement == NULL) {
        return false;
    }
    target = program_expression(program, statement->target_expression);
    expression = program_expression(program, statement->expression);

    switch (statement->kind) {
    case MINIC_STATEMENT_ASSIGN:
        return target != NULL && expression != NULL &&
               target->value_category == MINIC_VALUE_LVALUE &&
               minic_c0_assignment_compatible(program, target->type, statement->expression);
    case MINIC_STATEMENT_RECORD_COPY:
        return target != NULL && expression != NULL &&
               target->value_category == MINIC_VALUE_LVALUE &&
               minic_c0_record_value_is_copy_source(program, statement->expression) &&
               !minic_type_is_const(target->type) && minic_type_is_record(target->type) &&
               minic_type_is_record(expression->type) &&
               target->type.record_id == expression->type.record_id;
    case MINIC_STATEMENT_XOR_ASSIGN: {
        MinicType common_type;

        return target != NULL && expression != NULL &&
               target->value_category == MINIC_VALUE_LVALUE &&
               minic_type_is_integer(target->type) && minic_type_is_integer(expression->type) &&
               minic_target_info_integer_common(
                   target_info, target->type, expression->type, &common_type);
    }
    case MINIC_STATEMENT_EXPRESSION:
        return expression != NULL;
    case MINIC_STATEMENT_INLINE_ASM: {
        const MinicInlineAsm *inline_asm;
        size_t clobber_index;
        size_t operand_index;

        inline_asm = minic_c0_program_inline_asm(program, statement->inline_asm_id);
        if (inline_asm == NULL || inline_asm->template_text == NULL ||
            inline_asm->output_count > inline_asm->output_capacity ||
            inline_asm->input_count > inline_asm->input_capacity ||
            inline_asm->label_count > inline_asm->label_capacity ||
            inline_asm->register_clobber_count > inline_asm->register_clobber_capacity ||
            (inline_asm->output_count != 0U && inline_asm->outputs == NULL) ||
            (inline_asm->input_count != 0U && inline_asm->inputs == NULL) ||
            (inline_asm->label_count != 0U && inline_asm->labels == NULL) ||
            (inline_asm->register_clobber_count != 0U && inline_asm->register_clobbers == NULL) ||
            (inline_asm->is_goto ? (inline_asm->label_count == 0U || inline_asm->output_count != 0U)
                                 : inline_asm->label_count != 0U) ||
            (inline_asm->has_memory_clobber && inline_asm->register_clobber_count == SIZE_MAX) ||
            inline_asm->clobber_count !=
                inline_asm->register_clobber_count + (inline_asm->has_memory_clobber ? 1U : 0U) ||
            statement->target_expression != MINIC_EXPRESSION_INVALID ||
            statement->expression != MINIC_EXPRESSION_INVALID ||
            statement->target_statement != MINIC_STATEMENT_INVALID ||
            statement->then_block != MINIC_BLOCK_INVALID ||
            statement->else_block != MINIC_BLOCK_INVALID) {
            return false;
        }
        for (clobber_index = 0U; clobber_index < inline_asm->register_clobber_count;
             ++clobber_index) {
            if (inline_asm->register_clobbers[clobber_index].name == NULL ||
                inline_asm->register_clobbers[clobber_index].name_length == 0U) {
                return false;
            }
        }
        for (operand_index = 0U; operand_index < inline_asm->output_count; ++operand_index) {
            const MinicInlineAsmOperand *operand;
            const MinicExpression *operand_expression;

            operand = &inline_asm->outputs[operand_index];
            operand_expression = minic_c0_program_expression(program, operand->expression);
            if (operand->constraint_text == NULL || operand->constraint_length == 0U ||
                ((operand->name == NULL) != (operand->name_length == 0U)) ||
                operand_expression == NULL ||
                operand_expression->value_category != MINIC_VALUE_LVALUE ||
                (operand->access != MINIC_INLINE_ASM_OPERAND_WRITE_ONLY &&
                 operand->access != MINIC_INLINE_ASM_OPERAND_READ_WRITE)) {
                return false;
            }
        }
        for (operand_index = 0U; operand_index < inline_asm->input_count; ++operand_index) {
            const MinicInlineAsmOperand *operand;
            const MinicExpression *operand_expression;

            operand = &inline_asm->inputs[operand_index];
            operand_expression = minic_c0_program_expression(program, operand->expression);
            if (operand->constraint_text == NULL || operand->constraint_length == 0U ||
                ((operand->name == NULL) != (operand->name_length == 0U)) ||
                operand_expression == NULL ||
                operand->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY) {
                return false;
            }
        }
        for (operand_index = 0U; operand_index < inline_asm->label_count; ++operand_index) {
            const MinicInlineAsmLabel *label;
            const MinicStatement *target_statement;

            label = &inline_asm->labels[operand_index];
            target_statement = minic_c0_program_statement(program, label->target_statement);
            if (label->name == NULL || label->name_length == 0U || target_statement == NULL ||
                target_statement->kind != MINIC_STATEMENT_LABEL) {
                return false;
            }
        }
        return true;
    }

    case MINIC_STATEMENT_RETURN:
        return statement->expression == MINIC_EXPRESSION_INVALID || expression != NULL;
    case MINIC_STATEMENT_BREAK:
        return statement->target_expression == MINIC_EXPRESSION_INVALID &&
               statement->expression == MINIC_EXPRESSION_INVALID;
    case MINIC_STATEMENT_GOTO: {
        const MinicStatement *target_statement;

        target_statement = minic_c0_program_statement(program, statement->target_statement);
        return statement->target_expression == MINIC_EXPRESSION_INVALID &&
               statement->expression == MINIC_EXPRESSION_INVALID && target_statement != NULL &&
               target_statement->kind == MINIC_STATEMENT_LABEL &&
               statement->then_block == MINIC_BLOCK_INVALID &&
               statement->else_block == MINIC_BLOCK_INVALID;
    }
    case MINIC_STATEMENT_LABEL:
        return statement->target_expression == MINIC_EXPRESSION_INVALID &&
               statement->expression == MINIC_EXPRESSION_INVALID &&
               statement->target_statement == MINIC_STATEMENT_INVALID &&
               statement->then_block == MINIC_BLOCK_INVALID &&
               statement->else_block == MINIC_BLOCK_INVALID;
    case MINIC_STATEMENT_IF:
        return expression != NULL && type_is_condition_scalar(expression->type) &&
               statement->then_block < program->block_count &&
               (statement->else_block == MINIC_BLOCK_INVALID ||
                statement->else_block < program->block_count);
    case MINIC_STATEMENT_WHILE:
        return (statement->expression == MINIC_EXPRESSION_INVALID ||
                (expression != NULL && type_is_condition_scalar(expression->type))) &&
               statement->then_block < program->block_count;
    case MINIC_STATEMENT_SWITCH:
        return statement->target_expression == MINIC_EXPRESSION_INVALID && expression != NULL &&
               minic_type_is_integer(expression->type) &&
               statement->then_block < program->block_count &&
               statement->else_block == MINIC_BLOCK_INVALID;
    case MINIC_STATEMENT_CASE:
        return statement->target_expression == MINIC_EXPRESSION_INVALID && expression != NULL &&
               expression->kind == MINIC_EXPRESSION_INTEGER &&
               minic_type_is_integer(expression->type) &&
               statement->then_block == MINIC_BLOCK_INVALID &&
               statement->else_block == MINIC_BLOCK_INVALID;
    case MINIC_STATEMENT_DEFAULT:
        return statement->target_expression == MINIC_EXPRESSION_INVALID &&
               statement->expression == MINIC_EXPRESSION_INVALID &&
               statement->then_block == MINIC_BLOCK_INVALID &&
               statement->else_block == MINIC_BLOCK_INVALID;
    }
    return false;
}

static bool verify_program_storage(const MinicC0Program *program) {
    return program != NULL &&
           storage_is_valid(
               program->expressions, program->expression_count, program->expression_capacity) &&
           storage_is_valid(program->locals, program->local_count, program->local_capacity) &&
           storage_is_valid(
               program->statements, program->statement_count, program->statement_capacity) &&
           storage_is_valid(
               program->inline_asms, program->inline_asm_count, program->inline_asm_capacity) &&
           storage_is_valid(program->blocks, program->block_count, program->block_capacity) &&
           storage_is_valid(
               program->functions, program->function_count, program->function_capacity) &&
           storage_is_valid(program->records, program->record_count, program->record_capacity) &&
           storage_is_valid(
               program->array_types, program->array_type_count, program->array_type_capacity) &&
           storage_is_valid(program->function_types,
                            program->function_type_count,
                            program->function_type_capacity) &&
           storage_is_valid(
               program->type_aliases, program->type_alias_count, program->type_alias_capacity) &&
           storage_is_valid(program->enums, program->enum_count, program->enum_capacity) &&
           storage_is_valid(
               program->enumerators, program->enumerator_count, program->enumerator_capacity) &&
           storage_is_valid(program->global_objects,
                            program->global_object_count,
                            program->global_object_capacity);
}

static bool
type_owns_array_descriptor(MinicType type, MinicArrayTypeId array_type_id, bool require_pointer) {
    return type.base_kind == MINIC_TYPE_BASE_ARRAY && type.array_type_id == array_type_id &&
           (!require_pointer || type.pointer_depth != 0U);
}

static bool incomplete_array_has_semantic_owner(const MinicC0Program *program,
                                                MinicArrayTypeId array_type_id) {
    size_t index;

    if (program == NULL) {
        return false;
    }
    for (index = 0U; index < program->expression_count; ++index) {
        if (type_owns_array_descriptor(program->expressions[index].type, array_type_id, false)) {
            return true;
        }
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        if (type_owns_array_descriptor(program->type_aliases[index].type, array_type_id, false)) {
            return true;
        }
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object;

        object = &program->global_objects[index];
        if ((object->is_extern && minic_type_is_array(object->type) &&
             object->type.array_type_id == array_type_id) ||
            type_owns_array_descriptor(object->type, array_type_id, true)) {
            return true;
        }
    }
    for (index = 0U; index < program->local_count; ++index) {
        if (type_owns_array_descriptor(program->locals[index].type, array_type_id, true)) {
            return true;
        }
    }
    for (index = 0U; index < program->record_count; ++index) {
        const MinicRecord *record;
        size_t field_index;

        record = &program->records[index];
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            if (type_owns_array_descriptor(record->fields[field_index].type, array_type_id, true)) {
                return true;
            }
        }
    }
    for (index = 0U; index < program->function_count; ++index) {
        const MinicFunction *function;
        size_t parameter_index;

        function = &program->functions[index];
        if (type_owns_array_descriptor(function->return_type, array_type_id, true)) {
            return true;
        }
        for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
            if (type_owns_array_descriptor(
                    function->parameter_types[parameter_index], array_type_id, true)) {
                return true;
            }
        }
    }
    for (index = 0U; index < program->function_type_count; ++index) {
        const MinicFunctionType *function_type;
        size_t parameter_index;

        function_type = &program->function_types[index];
        if (type_owns_array_descriptor(function_type->return_type, array_type_id, true)) {
            return true;
        }
        for (parameter_index = 0U; parameter_index < function_type->parameter_count;
             ++parameter_index) {
            if (type_owns_array_descriptor(
                    function_type->parameter_types[parameter_index], array_type_id, true)) {
                return true;
            }
        }
    }
    return false;
}

bool minic_c0_program_verify_target(const MinicC0Program *program,
                                    MinicC0AstForm form,
                                    const MinicTargetInfo *target) {
    size_t index;

    if (target == NULL || (form != MINIC_C0_AST_PARSED && form != MINIC_C0_AST_NORMALIZED) ||
        !verify_program_storage(program)) {
        return false;
    }

    for (index = 0U; index < program->enum_count; ++index) {
        const MinicEnum *entity;

        entity = &program->enums[index];
        if ((entity->name == NULL) != (entity->name_length == 0U) ||
            !minic_type_is_integer(entity->compatible_type) ||
            minic_type_is_enum(entity->compatible_type) ||
            entity->compatible_type.pointer_depth != 0U) {
            return false;
        }
    }
    for (index = 0U; index < program->enumerator_count; ++index) {
        const MinicEnumerator *enumerator;

        enumerator = &program->enumerators[index];
        if (enumerator->name == NULL || enumerator->name_length == 0U ||
            enumerator->enum_id >= program->enum_count ||
            !minic_type_is_integer(enumerator->type) || minic_type_is_enum(enumerator->type)) {
            return false;
        }
    }
    for (index = 0U; index < program->array_type_count; ++index) {
        const MinicArrayType *array_type;

        array_type = &program->array_types[index];
        if ((array_type->element_count == 0U &&
             !incomplete_array_has_semantic_owner(program, index)) ||
            !type_is_valid(program, target, array_type->element_type) ||
            minic_type_is_function(array_type->element_type)) {
            return false;
        }
    }
    for (index = 0U; index < program->function_type_count; ++index) {
        const MinicFunctionType *function_type;
        size_t parameter_index;

        function_type = &program->function_types[index];
        if (function_type->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
            !type_is_valid(program, target, function_type->return_type) ||
            minic_type_is_array(function_type->return_type) ||
            minic_type_is_function(function_type->return_type)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < function_type->parameter_count;
             ++parameter_index) {
            if (!type_is_valid(program, target, function_type->parameter_types[parameter_index]) ||
                !type_is_top_level_unqualified(function_type->parameter_types[parameter_index]) ||
                minic_type_is_void(function_type->parameter_types[parameter_index]) ||
                minic_type_is_function(function_type->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < program->record_count; ++index) {
        const MinicRecord *record;
        size_t field_index;

        record = &program->records[index];
        if (record->name == NULL ||
            !storage_is_valid(record->fields, record->field_count, record->field_capacity)) {
            return false;
        }
        for (field_index = 0U; field_index < record->field_count; ++field_index) {
            const MinicRecordField *field;

            field = &record->fields[field_index];
            if (field->name == NULL || field->element_count == 0U ||
                !type_is_valid(program, target, field->type) ||
                minic_type_is_function(field->type)) {
                return false;
            }
        }
    }
    for (index = 0U; index < program->local_count; ++index) {
        if (program->locals[index].element_count == 0U ||
            !type_is_valid(program, target, program->locals[index].type) ||
            minic_type_is_function(program->locals[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < program->function_count; ++index) {
        const MinicFunction *function;
        size_t parameter_index;

        function = &program->functions[index];
        if (function->name == NULL || function->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
            !type_is_valid(program, target, function->return_type) ||
            minic_type_is_function(function->return_type) ||
            minic_type_is_array(function->return_type) ||
            function->local_begin > program->local_count ||
            function->local_count > program->local_count - function->local_begin ||
            (function->is_internal && function->is_weak) ||
            (function->is_defined && function->body_block >= program->block_count) ||
            (!function->is_defined && function->body_block != MINIC_BLOCK_INVALID)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
            if (!type_is_valid(program, target, function->parameter_types[parameter_index]) ||
                !type_is_top_level_unqualified(function->parameter_types[parameter_index]) ||
                minic_type_is_void(function->parameter_types[parameter_index]) ||
                minic_type_is_function(function->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        if (program->type_aliases[index].name == NULL ||
            !type_is_valid(program, target, program->type_aliases[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object;

        object = &program->global_objects[index];
        if (object->name == NULL || !type_is_valid(program, target, object->type) ||
            minic_type_is_function(object->type) ||
            (minic_type_is_void(object->type) && !object->is_extern) ||
            (object->is_extern &&
             (object->is_tentative || object->is_internal || object->is_zero_initialized ||
              object->initializer_count != 0U || object->relocation_count != 0U)) ||
            (object->is_tentative &&
             (object->is_extern || object->is_zero_initialized || object->initializer_count != 0U ||
              object->relocation_count != 0U)) ||
            (object->is_zero_initialized && object->initializer_count != 0U) ||
            (object->relocation_count != 0U && !object->is_zero_initialized &&
             ((!minic_type_is_record(object->type) && !minic_type_is_array(object->type)) ||
              object->initializer_count == 0U)) ||
            !storage_is_valid(object->initializer_values,
                              object->initializer_count,
                              object->initializer_capacity) ||
            !storage_is_valid(
                object->relocations, object->relocation_count, object->relocation_capacity)) {
            return false;
        }
        if (object->relocation_count != 0U && !object->is_zero_initialized) {
            size_t relocation_index;

            for (relocation_index = 0U; relocation_index < object->relocation_count;
                 ++relocation_index) {
                const MinicGlobalRelocation *relocation;

                relocation = &object->relocations[relocation_index];
                if ((relocation->location_kind != MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD &&
                     relocation->location_kind !=
                         MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR) ||
                    relocation->location_index >= object->initializer_count ||
                    object->initializer_values[relocation->location_index] != 0U) {
                    return false;
                }
                if (relocation->location_kind == MINIC_GLOBAL_RELOCATION_LOCATION_RECORD_FIELD) {
                    const MinicRecord *record;

                    record = minic_type_is_record(object->type)
                                 ? minic_c0_program_record(program, object->type.record_id)
                                 : NULL;
                    if (record == NULL || !record->is_complete || record->is_union ||
                        object->initializer_count != record->field_count) {
                        return false;
                    }
                }
            }
        }
        {
            size_t relocation_index;

            for (relocation_index = 0U; relocation_index < object->relocation_count;
                 ++relocation_index) {
                const MinicGlobalRelocation *relocation;
                MinicType slot_pointee;
                MinicType slot_type;

                relocation = &object->relocations[relocation_index];
                if (!minic_c0_global_relocation_slot_type(program,
                                                          object,
                                                          relocation->location_kind,
                                                          relocation->location_index,
                                                          &slot_type)) {
                    return false;
                }
                if (!minic_type_pointee(slot_type, &slot_pointee) ||
                    (relocation_index != 0U &&
                     (object->relocations[relocation_index - 1U].location_kind !=
                          relocation->location_kind ||
                      object->relocations[relocation_index - 1U].location_index >=
                          relocation->location_index)) ||
                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
                     (relocation->target_id >= program->global_object_count ||
                      (minic_type_is_function(slot_pointee) &&
                       !relocation->has_explicit_pointer_cast))) ||
                    (relocation->target_kind == MINIC_GLOBAL_RELOCATION_FUNCTION &&
                     !minic_c0_global_relocation_function_target_compatible(
                         program,
                         slot_type,
                         (MinicFunctionId)relocation->target_id,
                         relocation->has_explicit_pointer_cast)) ||
                    (relocation->target_kind != MINIC_GLOBAL_RELOCATION_OBJECT &&
                     relocation->target_kind != MINIC_GLOBAL_RELOCATION_FUNCTION)) {
                    return false;
                }
                if (relocation->target_kind == MINIC_GLOBAL_RELOCATION_OBJECT &&
                    !minic_c0_global_relocation_object_target_compatible(
                        program, relocation, slot_type)) {
                    return false;
                }
                {
                    size_t target_addend;

                    if (!minic_data_layout_global_relocation_target_addend(
                            minic_target_info_data_layout(target),
                            program,
                            relocation,
                            &target_addend)) {
                        return false;
                    }
                    (void)target_addend;
                }
            }
        }
    }
    if (!storage_is_valid(
            program->file_asms, program->file_asm_count, program->file_asm_capacity)) {
        return false;
    }
    for (index = 0U; index < program->file_asm_count; ++index) {
        const MinicFileAsm *file_asm;

        file_asm = &program->file_asms[index];
        if (file_asm->text == NULL || strlen(file_asm->text) != file_asm->length) {
            return false;
        }
    }
    for (index = 0U; index < program->expression_count; ++index) {
        if (!verify_expression(program, index, form, target)) {
            fprintf(stderr,
                    "VERIFY_FAIL expression=%zu kind=%d form=%d\n",
                    index,
                    (int)program->expressions[index].kind,
                    (int)form);
            return false;
        }
    }
    for (index = 0U; index < program->statement_count; ++index) {
        if (!verify_statement(program, target, &program->statements[index])) {
            fprintf(stderr,
                    "VERIFY_FAIL statement=%zu kind=%d\n",
                    index,
                    (int)program->statements[index].kind);
            return false;
        }
    }
    for (index = 0U; index < program->block_count; ++index) {
        const MinicBlock *block;
        size_t statement_index;

        block = &program->blocks[index];
        if (!storage_is_valid(
                block->statements, block->statement_count, block->statement_capacity)) {
            return false;
        }
        for (statement_index = 0U; statement_index < block->statement_count; ++statement_index) {
            if (block->statements[statement_index] >= program->statement_count) {
                return false;
            }
        }
    }

    return (program->body_block == MINIC_BLOCK_INVALID ||
            program->body_block < program->block_count) &&
           (program->entry_function == MINIC_FUNCTION_INVALID ||
            program->entry_function < program->function_count) &&
           (program->return_expression == MINIC_EXPRESSION_INVALID ||
            program->return_expression < program->expression_count);
}

bool minic_c0_program_verify(const MinicC0Program *program, MinicC0AstForm form) {
    return minic_c0_program_verify_target(program, form, minic_default_target_info());
}
