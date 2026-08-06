#include "frontend/ast_verifier.h"

static bool storage_is_valid(
    const void *data,
    size_t count,
    size_t capacity)
{
    return count <= capacity && (count == 0U || data != NULL);
}

static bool type_is_valid(
    const MinicC0Program *program,
    MinicType type)
{
    if (program == NULL ||
        (type.base_qualifiers &
         ~((unsigned int)MINIC_TYPE_QUALIFIER_CONST)) != 0U) {
        return false;
    }

    switch (type.base_kind) {
    case MINIC_TYPE_BASE_VOID:
        return type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
               type.integer_rank == MINIC_INTEGER_RANK_NONE;
    case MINIC_TYPE_BASE_INT:
        return type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               (type.integer_sign == MINIC_INTEGER_SIGN_SIGNED ||
                type.integer_sign == MINIC_INTEGER_SIGN_UNSIGNED) &&
               (type.integer_rank == MINIC_INTEGER_RANK_CHAR ||
                type.integer_rank == MINIC_INTEGER_RANK_INT);
    case MINIC_TYPE_BASE_RECORD:
        return type.record_id < program->record_count &&
               type.array_type_id == MINIC_ARRAY_TYPE_INVALID &&
               type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
               type.integer_rank == MINIC_INTEGER_RANK_NONE;
    case MINIC_TYPE_BASE_ARRAY:
        return type.record_id == MINIC_RECORD_INVALID &&
               type.array_type_id < program->array_type_count &&
               type.integer_sign == MINIC_INTEGER_SIGN_NONE &&
               type.integer_rank == MINIC_INTEGER_RANK_NONE;
    }
    return false;
}

static bool type_is_complete_object_bounded(
    const MinicC0Program *program,
    MinicType type,
    size_t remaining_depth)
{
    if (remaining_depth == 0U || minic_type_is_void(type)) {
        return false;
    }
    if (minic_type_is_integer(type) || minic_type_is_pointer(type)) {
        return true;
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;

        record = minic_c0_program_record(program, type.record_id);
        return record != NULL && record->is_complete;
    }
    if (minic_type_is_array(type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(
            program,
            type.array_type_id);
        return array_type != NULL && array_type->element_count != 0U &&
               type_is_complete_object_bounded(
                   program,
                   array_type->element_type,
                   remaining_depth - 1U);
    }
    return false;
}

static bool type_is_complete_object(
    const MinicC0Program *program,
    MinicType type)
{
    return type_is_complete_object_bounded(
        program,
        type,
        program->array_type_count + program->record_count + 1U);
}

static const MinicExpression *expression_before(
    const MinicC0Program *program,
    MinicExpressionId expression_id,
    size_t parent_index)
{
    if (program == NULL || expression_id >= parent_index ||
        expression_id >= program->expression_count) {
        return NULL;
    }
    return &program->expressions[expression_id];
}

static const MinicExpression *program_expression(
    const MinicC0Program *program,
    MinicExpressionId expression_id)
{
    if (program == NULL || expression_id >= program->expression_count) {
        return NULL;
    }
    return &program->expressions[expression_id];
}

static bool binary_is_comparison(MinicBinaryOperator operator_kind)
{
    return operator_kind == MINIC_BINARY_EQUAL ||
           operator_kind == MINIC_BINARY_NOT_EQUAL ||
           operator_kind == MINIC_BINARY_LESS ||
           operator_kind == MINIC_BINARY_LESS_EQUAL ||
           operator_kind == MINIC_BINARY_GREATER ||
           operator_kind == MINIC_BINARY_GREATER_EQUAL;
}

static bool binary_is_shift(MinicBinaryOperator operator_kind)
{
    return operator_kind == MINIC_BINARY_SHIFT_LEFT ||
           operator_kind == MINIC_BINARY_SHIFT_RIGHT;
}

static bool binary_operator_is_valid(MinicBinaryOperator operator_kind)
{
    return operator_kind >= MINIC_BINARY_ADD &&
           operator_kind <= MINIC_BINARY_GREATER_EQUAL;
}

static bool unary_operator_is_valid(MinicUnaryOperator operator_kind)
{
    return operator_kind >= MINIC_UNARY_PLUS &&
           operator_kind <= MINIC_UNARY_LOGICAL_NOT;
}

static bool is_normalized_integer_cast_add(
    const MinicExpression *expression,
    const MinicExpression *left,
    const MinicExpression *right,
    MinicC0AstForm form)
{
    return form == MINIC_C0_AST_NORMALIZED &&
           expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
           right->kind == MINIC_EXPRESSION_INTEGER &&
           right->value.integer_value == 0 &&
           minic_type_equal(right->type, minic_type_int()) &&
           minic_type_is_integer(left->type) &&
           minic_type_is_integer(expression->type) &&
           minic_type_cast_compatible(expression->type, left->type);
}

static bool verify_binary_type(
    const MinicC0Program *program,
    const MinicExpression *expression,
    const MinicExpression *left,
    const MinicExpression *right,
    MinicC0AstForm form)
{
    MinicType expected_type;
    MinicType pointer_type;
    MinicType pointee_type;

    if (!binary_operator_is_valid(
            expression->value.binary.operator_kind)) {
        return false;
    }

    if (minic_type_is_integer(left->type) &&
        minic_type_is_integer(right->type)) {
        if (binary_is_comparison(
                expression->value.binary.operator_kind)) {
            expected_type = minic_type_int();
        } else if (binary_is_shift(
                       expression->value.binary.operator_kind)) {
            if (!minic_type_integer_promotion(
                    left->type,
                    &expected_type)) {
                return false;
            }
        } else if (!minic_type_integer_common(
                       left->type,
                       right->type,
                       &expected_type)) {
            return false;
        }
        return minic_type_equal(expression->type, expected_type) ||
               is_normalized_integer_cast_add(
                   expression,
                   left,
                   right,
                   form);
    }

    if (expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
        if (minic_type_is_pointer(left->type) &&
            minic_type_is_integer(right->type)) {
            pointer_type = left->type;
        } else if (minic_type_is_integer(left->type) &&
                   minic_type_is_pointer(right->type)) {
            pointer_type = right->type;
        } else {
            return false;
        }
    } else if (expression->value.binary.operator_kind ==
                   MINIC_BINARY_SUBTRACT &&
               minic_type_is_pointer(left->type) &&
               minic_type_is_integer(right->type)) {
        pointer_type = left->type;
    } else {
        return false;
    }

    return minic_type_equal(expression->type, pointer_type) &&
           minic_type_pointee(pointer_type, &pointee_type) &&
           type_is_complete_object(program, pointee_type);
}

static bool verify_subscript_type(
    const MinicC0Program *program,
    const MinicExpression *base,
    MinicType result_type)
{
    MinicType pointee_type;

    if (base->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(
            program,
            base->value.local_id);
        if (local != NULL && local->element_count > 1U) {
            return minic_type_equal(local->type, result_type);
        }
    }
    if (minic_type_is_array(base->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(
            program,
            base->type.array_type_id);
        return array_type != NULL &&
               minic_type_equal(array_type->element_type, result_type);
    }
    if (minic_type_pointee(base->type, &pointee_type)) {
        return minic_type_equal(pointee_type, result_type);
    }
    return false;
}

static bool verify_expression(
    const MinicC0Program *program,
    size_t expression_index,
    MinicC0AstForm form)
{
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
    case MINIC_EXPRESSION_LOCAL: {
        const MinicLocal *local;

        local = minic_c0_program_local(
            program,
            expression->value.local_id);
        return local != NULL &&
               expression->value_category == MINIC_VALUE_LVALUE &&
               minic_type_equal(expression->type, local->type);
    }
    case MINIC_EXPRESSION_GLOBAL_OBJECT: {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(
            program,
            expression->value.global_object_id);
        return object != NULL &&
               expression->value_category == MINIC_VALUE_LVALUE &&
               minic_type_equal(expression->type, object->type);
    }
    case MINIC_EXPRESSION_ADDRESS_OF: {
        MinicType pointee_type;

        operand = expression_before(
            program,
            expression->value.unary.operand,
            expression_index);
        return operand != NULL &&
               operand->value_category == MINIC_VALUE_LVALUE &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_pointee(expression->type, &pointee_type) &&
               minic_type_equal(pointee_type, operand->type);
    }
    case MINIC_EXPRESSION_DEREFERENCE: {
        MinicType pointee_type;

        operand = expression_before(
            program,
            expression->value.unary.operand,
            expression_index);
        return operand != NULL &&
               expression->value_category == MINIC_VALUE_LVALUE &&
               minic_type_pointee(operand->type, &pointee_type) &&
               minic_type_equal(expression->type, pointee_type);
    }
    case MINIC_EXPRESSION_CAST:
        operand = expression_before(
            program,
            expression->value.unary.operand,
            expression_index);
        return form == MINIC_C0_AST_PARSED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_cast_compatible(
                   expression->type,
                   operand->type);
    case MINIC_EXPRESSION_BITCAST:
        operand = expression_before(
            program,
            expression->value.unary.operand,
            expression_index);
        return form == MINIC_C0_AST_NORMALIZED && operand != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_is_pointer(expression->type) &&
               minic_type_is_pointer(operand->type) &&
               minic_type_cast_compatible(
                   expression->type,
                   operand->type);
    case MINIC_EXPRESSION_SUBSCRIPT:
        left = expression_before(
            program,
            expression->value.subscript.base,
            expression_index);
        right = expression_before(
            program,
            expression->value.subscript.index,
            expression_index);
        return left != NULL && right != NULL &&
               expression->value_category == MINIC_VALUE_LVALUE &&
               minic_type_is_integer(right->type) &&
               verify_subscript_type(program, left, expression->type);
    case MINIC_EXPRESSION_MEMBER: {
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicType record_type;
        MinicType expected_type;

        operand = expression_before(
            program,
            expression->value.member.base,
            expression_index);
        record = minic_c0_program_record(
            program,
            expression->value.member.record_id);
        field = minic_c0_record_field(
            record,
            expression->value.member.field_index);
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

        operand = expression_before(
            program,
            expression->value.unary.operand,
            expression_index);
        if (operand == NULL ||
            expression->value_category != MINIC_VALUE_RVALUE ||
            !minic_type_is_integer(operand->type) ||
            !unary_operator_is_valid(
                expression->value.unary.operator_kind)) {
            return false;
        }
        if (expression->value.unary.operator_kind ==
            MINIC_UNARY_LOGICAL_NOT) {
            expected_type = minic_type_int();
        } else if (!minic_type_integer_promotion(
                       operand->type,
                       &expected_type)) {
            return false;
        }
        return minic_type_equal(expression->type, expected_type);
    }
    case MINIC_EXPRESSION_BINARY:
        left = expression_before(
            program,
            expression->value.binary.left,
            expression_index);
        right = expression_before(
            program,
            expression->value.binary.right,
            expression_index);
        return left != NULL && right != NULL &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               verify_binary_type(
                   program,
                   expression,
                   left,
                   right,
                   form);
    case MINIC_EXPRESSION_CALL: {
        const MinicFunction *function;
        size_t argument_index;

        function = minic_c0_program_function(
            program,
            expression->value.call.function_id);
        if (function == NULL ||
            expression->value.call.argument_count !=
                function->parameter_count ||
            expression->value.call.argument_count > 8U ||
            expression->value_category != MINIC_VALUE_RVALUE ||
            !minic_type_equal(expression->type, function->return_type)) {
            return false;
        }
        for (argument_index = 0U;
             argument_index < expression->value.call.argument_count;
             ++argument_index) {
            operand = expression_before(
                program,
                expression->value.call.arguments[argument_index],
                expression_index);
            if (operand == NULL ||
                !minic_type_assignment_compatible(
                    function->parameter_types[argument_index],
                    operand->type)) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

static bool verify_statement(
    const MinicC0Program *program,
    const MinicStatement *statement)
{
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
               minic_type_assignment_compatible(
                   target->type,
                   expression->type);
    case MINIC_STATEMENT_XOR_ASSIGN: {
        MinicType common_type;

        return target != NULL && expression != NULL &&
               target->value_category == MINIC_VALUE_LVALUE &&
               minic_type_is_integer(target->type) &&
               minic_type_is_integer(expression->type) &&
               minic_type_integer_common(
                   target->type,
                   expression->type,
                   &common_type);
    }
    case MINIC_STATEMENT_EXPRESSION:
        return expression != NULL;
    case MINIC_STATEMENT_RETURN:
        return statement->expression == MINIC_EXPRESSION_INVALID ||
               expression != NULL;
    case MINIC_STATEMENT_BREAK:
        return statement->target_expression == MINIC_EXPRESSION_INVALID &&
               statement->expression == MINIC_EXPRESSION_INVALID;
    case MINIC_STATEMENT_IF:
        return expression != NULL &&
               minic_type_is_integer(expression->type) &&
               statement->then_block < program->block_count &&
               (statement->else_block == MINIC_BLOCK_INVALID ||
                statement->else_block < program->block_count);
    case MINIC_STATEMENT_WHILE:
        return (statement->expression == MINIC_EXPRESSION_INVALID ||
                (expression != NULL &&
                 minic_type_is_integer(expression->type))) &&
               statement->then_block < program->block_count;
    }
    return false;
}

static bool verify_program_storage(const MinicC0Program *program)
{
    return program != NULL &&
           storage_is_valid(
               program->expressions,
               program->expression_count,
               program->expression_capacity) &&
           storage_is_valid(
               program->locals,
               program->local_count,
               program->local_capacity) &&
           storage_is_valid(
               program->statements,
               program->statement_count,
               program->statement_capacity) &&
           storage_is_valid(
               program->blocks,
               program->block_count,
               program->block_capacity) &&
           storage_is_valid(
               program->functions,
               program->function_count,
               program->function_capacity) &&
           storage_is_valid(
               program->records,
               program->record_count,
               program->record_capacity) &&
           storage_is_valid(
               program->array_types,
               program->array_type_count,
               program->array_type_capacity) &&
           storage_is_valid(
               program->type_aliases,
               program->type_alias_count,
               program->type_alias_capacity) &&
           storage_is_valid(
               program->global_objects,
               program->global_object_count,
               program->global_object_capacity);
}

bool minic_c0_program_verify(
    const MinicC0Program *program,
    MinicC0AstForm form)
{
    size_t index;

    if ((form != MINIC_C0_AST_PARSED &&
         form != MINIC_C0_AST_NORMALIZED) ||
        !verify_program_storage(program)) {
        return false;
    }

    for (index = 0U; index < program->array_type_count; ++index) {
        const MinicArrayType *array_type;

        array_type = &program->array_types[index];
        if (array_type->element_count == 0U ||
            !type_is_valid(program, array_type->element_type)) {
            return false;
        }
    }
    for (index = 0U; index < program->record_count; ++index) {
        const MinicRecord *record;
        size_t field_index;

        record = &program->records[index];
        if (record->name == NULL ||
            !storage_is_valid(
                record->fields,
                record->field_count,
                record->field_capacity)) {
            return false;
        }
        for (field_index = 0U;
             field_index < record->field_count;
             ++field_index) {
            const MinicRecordField *field;

            field = &record->fields[field_index];
            if (field->name == NULL || field->element_count == 0U ||
                !type_is_valid(program, field->type)) {
                return false;
            }
        }
    }
    for (index = 0U; index < program->local_count; ++index) {
        if (program->locals[index].element_count == 0U ||
            !type_is_valid(program, program->locals[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < program->function_count; ++index) {
        const MinicFunction *function;
        size_t parameter_index;

        function = &program->functions[index];
        if (function->name == NULL || function->parameter_count > 8U ||
            !type_is_valid(program, function->return_type) ||
            function->local_begin > program->local_count ||
            function->local_count >
                program->local_count - function->local_begin ||
            (function->is_defined &&
             function->body_block >= program->block_count) ||
            (!function->is_defined &&
             function->body_block != MINIC_BLOCK_INVALID)) {
            return false;
        }
        for (parameter_index = 0U;
             parameter_index < function->parameter_count;
             ++parameter_index) {
            if (!type_is_valid(
                    program,
                    function->parameter_types[parameter_index]) ||
                minic_type_is_void(
                    function->parameter_types[parameter_index])) {
                return false;
            }
        }
    }
    for (index = 0U; index < program->type_alias_count; ++index) {
        if (program->type_aliases[index].name == NULL ||
            !type_is_valid(program, program->type_aliases[index].type)) {
            return false;
        }
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object;

        object = &program->global_objects[index];
        if (object->name == NULL ||
            !type_is_valid(program, object->type) ||
            !storage_is_valid(
                object->initializer_values,
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
                block->statements,
                block->statement_count,
                block->statement_capacity)) {
            return false;
        }
        for (statement_index = 0U;
             statement_index < block->statement_count;
             ++statement_index) {
            if (block->statements[statement_index] >=
                program->statement_count) {
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
