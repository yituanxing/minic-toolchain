#include "frontend/ast_verifier.h"

#include <limits.h>

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
    return (type.pointer_qualifiers >> type.pointer_depth) == 0U;
}

static bool type_is_valid(const MinicC0Program *program, MinicType type) {
    if (program == NULL ||
        (type.base_qualifiers & ~((unsigned int)MINIC_TYPE_QUALIFIER_CONST)) != 0U ||
        !pointer_qualifiers_are_valid(type)) {
        return false;
    }
    if (type.is_plain_char && type.base_kind != MINIC_TYPE_BASE_INT) {
        return false;
    }
    if (type.is_plain_char && type.integer_sign != MINIC_INTEGER_SIGN_UNSIGNED) {
        return false;
    }
    if (type.is_plain_char && type.integer_rank != MINIC_INTEGER_RANK_CHAR) {
        return false;
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
               (type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
                type.integer_rank == MINIC_INTEGER_RANK_INT ||
                type.integer_rank == MINIC_INTEGER_RANK_LONG);
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

static bool type_is_complete_object_bounded(const MinicC0Program *program,
                                            MinicType type,
                                            size_t remaining_depth) {
    if (remaining_depth == 0U || minic_type_is_void(type) || minic_type_is_function(type)) {
        return false;
    }
    if (minic_type_is_integer(type) || minic_type_is_float(type) || minic_type_is_double(type) ||
        minic_type_is_pointer(type)) {
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, type.record_id);
        return record != NULL && record->is_complete;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        return array_type != NULL && array_type->element_count != 0U &&
               type_is_complete_object_bounded(
                   program, array_type->element_type, remaining_depth - 1U);
    }
    return false;
}

static bool type_is_complete_object(const MinicC0Program *program, MinicType type) {
    size_t remaining_depth;

    remaining_depth = program->array_type_count;
    remaining_depth += program->record_count;
    remaining_depth += program->function_type_count;
    remaining_depth += 1U;
    return type_is_complete_object_bounded(program, type, remaining_depth);
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
    return operator_kind >= MINIC_BINARY_ADD && operator_kind <= MINIC_BINARY_LOGICAL_OR;
}

static bool unary_operator_is_valid(MinicUnaryOperator operator_kind) {
    return operator_kind >= MINIC_UNARY_PLUS && operator_kind <= MINIC_UNARY_POST_DECREMENT;
}

static bool expression_is_integer_zero(const MinicExpression *expression) {
    return expression != NULL && expression->kind == MINIC_EXPRESSION_INTEGER &&
           minic_type_is_integer(expression->type) && expression->value.integer_value == 0;
}

static bool type_is_condition_scalar(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}

static bool is_normalized_integer_cast_add(const MinicExpression *expression,
                                           const MinicExpression *left,
                                           const MinicExpression *right,
                                           MinicC0AstForm form) {
    return form == MINIC_C0_AST_NORMALIZED &&
           expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
           right->kind == MINIC_EXPRESSION_INTEGER && right->value.integer_value == 0 &&
           minic_type_equal(right->type, minic_type_int()) && minic_type_is_integer(left->type) &&
           minic_type_is_integer(expression->type) &&
           minic_type_cast_compatible(expression->type, left->type);
}

static bool verify_binary_type(const MinicC0Program *program,
                               const MinicExpression *expression,
                               const MinicExpression *left,
                               const MinicExpression *right,
                               MinicC0AstForm form) {
    MinicType expected_type;
    MinicType pointer_type;
    MinicType pointee_type;

    if (!binary_operator_is_valid(expression->value.binary.operator_kind)) {
        return false;
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

    if (minic_type_is_integer(left->type) && minic_type_is_integer(right->type)) {
        if (binary_is_comparison(expression->value.binary.operator_kind)) {
            expected_type = minic_type_int();
        } else if (binary_is_shift(expression->value.binary.operator_kind)) {
            if (!minic_type_integer_promotion(left->type, &expected_type)) {
                return false;
            }
        } else if (!minic_type_integer_common(left->type, right->type, &expected_type)) {
            return false;
        }
        return minic_type_equal(expression->type, expected_type) ||
               is_normalized_integer_cast_add(expression, left, right, form);
    }

    if (binary_is_comparison(expression->value.binary.operator_kind) &&
        (minic_type_is_double(left->type) || minic_type_is_integer(left->type)) &&
        (minic_type_is_double(right->type) || minic_type_is_integer(right->type)) &&
        (minic_type_is_double(left->type) || minic_type_is_double(right->type))) {
        return minic_type_equal(expression->type, minic_type_int());
    }

    if (minic_type_is_double(left->type) && minic_type_is_double(right->type) &&
        binary_is_double_arithmetic(expression->value.binary.operator_kind)) {
        return minic_type_is_double(expression->type);
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
           type_is_complete_object(program, pointee_type);
}

static bool verify_subscript_type(const MinicC0Program *program,
                                  const MinicExpression *base,
                                  MinicType result_type) {
    MinicType pointee_type;

    if (base->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(program, base->value.local_id);
        if (local != NULL && local->element_count > 1U) {
            return minic_type_equal(local->type, result_type);
        }
    }
    if (minic_type_is_array(base->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, base->type.array_type_id);
        return array_type != NULL && minic_type_equal(array_type->element_type, result_type);
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

    if (expression == NULL || parameter_count > 8U ||
        expression->value.call.argument_count < parameter_count ||
        expression->value.call.argument_count > 8U ||
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
            if (!minic_c0_assignment_compatible(program,
                                                parameter_types[argument_index],
                                                expression->value.call.arguments[argument_index])) {
                return false;
            }
        } else if (!minic_type_is_integer(argument->type) &&
                   !minic_type_is_pointer(argument->type)) {
            return false;
        }
    }
    return true;
}

static bool
verify_expression(const MinicC0Program *program, size_t expression_index, MinicC0AstForm form) {
    const MinicExpression *expression;
    const MinicExpression *left;
    const MinicExpression *right;
    const MinicExpression *operand;

    expression = &program->expressions[expression_index];
    if (!type_is_valid(program, expression->type) ||
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
    case MINIC_EXPRESSION_SIZEOF:
        return expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_unsigned_long()) &&
               type_is_valid(program, expression->value.sizeof_type) &&
               type_is_complete_object(program, expression->value.sizeof_type);
    case MINIC_EXPRESSION_ADDRESS_OF: {
        MinicType pointee_type;

        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return operand != NULL && operand->value_category == MINIC_VALUE_LVALUE &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_pointee(expression->type, &pointee_type) &&
               minic_type_equal(pointee_type, operand->type);
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
               (minic_type_cast_compatible(expression->type, operand->type) ||
                (minic_type_is_pointer(expression->type) && expression_is_integer_zero(operand)));
    case MINIC_EXPRESSION_BITCAST:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_pointer(expression->type) &&
               ((minic_type_is_pointer(operand->type) &&
                 minic_type_cast_compatible(expression->type, operand->type)) ||
                expression_is_integer_zero(operand));
    case MINIC_EXPRESSION_CONVERSION:
        operand = expression_before(program, expression->value.unary.operand, expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_double(expression->type) && minic_type_is_integer(operand->type);
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

        operand = expression_before(program, expression->value.member.base, expression_index);
        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (operand == NULL || record == NULL || field == NULL ||
            !minic_type_pointee(operand->type, &record_type) ||
            !minic_type_is_record(record_type) ||
            record_type.record_id != expression->value.member.record_id) {
            return false;
        }
        expected_type = field->type;
        if (minic_type_is_const(record_type) &&
            !minic_type_add_const(expected_type, &expected_type)) {
            return false;
        }
        if (field->element_count > 1U) {
            return expression->value_category == MINIC_VALUE_RVALUE &&
                   minic_type_pointer_to(expected_type, &expected_type) &&
                   minic_type_equal(expression->type, expected_type);
        }
        return expression->value_category == MINIC_VALUE_LVALUE &&
               minic_type_equal(expression->type, expected_type);
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
            expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT) {
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
                   type_is_complete_object(program, pointee_type);
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
            return type_is_condition_scalar(operand->type) &&
                   minic_type_equal(expression->type, minic_type_int());
        }
        if (!minic_type_is_integer(operand->type) ||
            !minic_type_integer_promotion(operand->type, &expected_type)) {
            return false;
        }
        return minic_type_equal(expression->type, expected_type);
    }
    case MINIC_EXPRESSION_BINARY:
        left = expression_before(program, expression->value.binary.left, expression_index);
        right = expression_before(program, expression->value.binary.right, expression_index);
        return left != NULL && right != NULL && expression->value_category == MINIC_VALUE_RVALUE &&
               verify_binary_type(program, expression, left, right, form);
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
            MinicType pointee;

            callee = expression_before(program, expression->value.call.callee, expression_index);
            if (callee == NULL || !minic_type_pointee(callee->type, &pointee) ||
                !minic_type_is_function(pointee)) {
                return false;
            }
            function_type = minic_c0_program_function_type(program, pointee.function_type_id);
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

static bool verify_statement(const MinicC0Program *program, const MinicStatement *statement) {
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
    case MINIC_STATEMENT_XOR_ASSIGN: {
        MinicType common_type;

        return target != NULL && expression != NULL &&
               target->value_category == MINIC_VALUE_LVALUE &&
               minic_type_is_integer(target->type) && minic_type_is_integer(expression->type) &&
               minic_type_integer_common(target->type, expression->type, &common_type);
    }
    case MINIC_STATEMENT_EXPRESSION:
        return expression != NULL;
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
           storage_is_valid(program->global_objects,
                            program->global_object_count,
                            program->global_object_capacity);
}

bool minic_c0_program_verify(const MinicC0Program *program, MinicC0AstForm form) {
    size_t index;

    if ((form != MINIC_C0_AST_PARSED && form != MINIC_C0_AST_NORMALIZED) ||
        !verify_program_storage(program)) {
        return false;
    }

    for (index = 0U; index < program->array_type_count; ++index) {
        const MinicArrayType *array_type;

        array_type = &program->array_types[index];
        if (array_type->element_count == 0U || !type_is_valid(program, array_type->element_type) ||
            minic_type_is_function(array_type->element_type)) {
            return false;
        }
    }
    for (index = 0U; index < program->function_type_count; ++index) {
        const MinicFunctionType *function_type;
        size_t parameter_index;

        function_type = &program->function_types[index];
        if (function_type->parameter_count > 8U ||
            !type_is_valid(program, function_type->return_type) ||
            minic_type_is_array(function_type->return_type) ||
            minic_type_is_function(function_type->return_type)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < function_type->parameter_count;
             ++parameter_index) {
            if (!type_is_valid(program, function_type->parameter_types[parameter_index]) ||
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
                !type_is_valid(program, field->type) || minic_type_is_function(field->type)) {
                return false;
            }
        }
    }
    for (index = 0U; index < program->local_count; ++index) {
        if (program->locals[index].element_count == 0U ||
            !type_is_valid(program, program->locals[index].type) ||
            minic_type_is_function(program->locals[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < program->function_count; ++index) {
        const MinicFunction *function;
        size_t parameter_index;

        function = &program->functions[index];
        if (function->name == NULL || function->parameter_count > 8U ||
            !type_is_valid(program, function->return_type) ||
            minic_type_is_function(function->return_type) ||
            minic_type_is_array(function->return_type) ||
            function->local_begin > program->local_count ||
            function->local_count > program->local_count - function->local_begin ||
            (function->is_defined && function->body_block >= program->block_count) ||
            (!function->is_defined && function->body_block != MINIC_BLOCK_INVALID)) {
            return false;
        }
        for (parameter_index = 0U; parameter_index < function->parameter_count; ++parameter_index) {
            if (!type_is_valid(program, function->parameter_types[parameter_index]) ||
                !type_is_top_level_unqualified(function->parameter_types[parameter_index]) ||
                minic_type_is_void(function->parameter_types[parameter_index]) ||
                minic_type_is_function(function->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        if (program->type_aliases[index].name == NULL ||
            !type_is_valid(program, program->type_aliases[index].type) ||
            minic_type_is_function(program->type_aliases[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object;

        object = &program->global_objects[index];
        if (object->name == NULL || !type_is_valid(program, object->type) ||
            minic_type_is_function(object->type) ||
            (object->is_zero_initialized && object->initializer_count != 0U) ||
            !storage_is_valid(object->initializer_values,
                              object->initializer_count,
                              object->initializer_capacity)) {
            return false;
        }
    }
    for (index = 0U; index < program->expression_count; ++index) {
        if (!verify_expression(program, index, form)) {
            return false;
        }
    }
    for (index = 0U; index < program->statement_count; ++index) {
        if (!verify_statement(program, &program->statements[index])) {
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
