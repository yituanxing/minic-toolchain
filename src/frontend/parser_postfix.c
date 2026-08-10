#include "frontend/parser_internal.h"

#include <string.h>

static bool postfix_element_type(const MinicParser *parser,
                                 MinicExpressionId base_id,
                                 MinicType *element_type) {
    const MinicExpression *base;

    if (element_type == NULL) {
        return false;
    }
    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL) {
        return false;
    }
    if (base->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(parser->program, base->value.local_id);
        if (local != NULL && local->is_array) {
            *element_type = local->type;
            return true;
        }
    }
    if (minic_type_is_array(base->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(parser->program, base->type.array_type_id);
        if (array_type == NULL) {
            return false;
        }
        *element_type = array_type->element_type;
        return true;
    }
    return minic_type_pointee(base->type, element_type);
}

static bool array_object_element_type(const MinicParser *parser,
                                      MinicExpressionId expression_id,
                                      MinicType *element_type) {
    const MinicExpression *expression;

    if (element_type == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(parser->program, expression->value.local_id);
        if (local != NULL && local->is_array) {
            *element_type = local->type;
            return true;
        }
    }
    if (minic_type_is_array(expression->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(parser->program, expression->type.array_type_id);
        if (array_type != NULL) {
            *element_type = array_type->element_type;
            return true;
        }
    }
    return false;
}

bool minic_parser_apply_array_decay(MinicParser *parser,
                                    MinicExpressionId input_id,
                                    MinicExpressionId *expression_id) {
    const MinicExpression *base;
    MinicSourceSpan base_span;
    MinicExpression zero;
    MinicExpression subscript;
    MinicExpression address;
    MinicExpressionId zero_id;
    MinicExpressionId subscript_id;
    MinicType element_type;

    base = minic_c0_program_expression(parser->program, input_id);
    if (base == NULL) {
        minic_parser_error(parser, "invalid array decay operand");
        return false;
    }
    base_span = base->span;
    if (!array_object_element_type(parser, input_id, &element_type)) {
        *expression_id = input_id;
        return true;
    }

    (void)memset(&zero, 0, sizeof(zero));
    zero.kind = MINIC_EXPRESSION_INTEGER;
    zero.span = base_span;
    zero.type = minic_type_int();
    zero.value_category = MINIC_VALUE_RVALUE;
    zero.value.integer_value = 0;
    if (!minic_parser_add_expression(parser, &zero, &zero_id)) {
        return false;
    }

    (void)memset(&subscript, 0, sizeof(subscript));
    subscript.kind = MINIC_EXPRESSION_SUBSCRIPT;
    subscript.span = base_span;
    subscript.type = element_type;
    subscript.value_category = MINIC_VALUE_LVALUE;
    subscript.value.subscript.base = input_id;
    subscript.value.subscript.index = zero_id;
    if (!minic_parser_add_expression(parser, &subscript, &subscript_id)) {
        return false;
    }

    (void)memset(&address, 0, sizeof(address));
    address.kind = MINIC_EXPRESSION_ADDRESS_OF;
    address.span = base_span;
    if (!minic_type_pointer_to(element_type, &address.type)) {
        minic_parser_error(parser, "array decay pointer depth is unsupported");
        return false;
    }
    address.value_category = MINIC_VALUE_RVALUE;
    address.value.unary.operand = subscript_id;
    return minic_parser_add_expression(parser, &address, expression_id);
}

static bool parse_one_subscript(MinicParser *parser,
                                MinicExpressionId base_id,
                                MinicExpressionId *expression_id) {
    const MinicExpression *base;
    MinicSourceSpan base_span;
    MinicType element_type;
    MinicExpressionId index_id;
    const MinicExpression *index_expression;
    MinicSourcePosition subscript_end;
    MinicExpression subscript;

    base = minic_c0_program_expression(parser->program, base_id);
    if (base == NULL || !postfix_element_type(parser, base_id, &element_type)) {
        minic_parser_error(parser, "subscript base must be an array or pointer");
        return false;
    }
    base_span = base->span;
    if (!minic_parser_advance(parser) || !minic_parser_parse_expression(parser, &index_id, 0U)) {
        return false;
    }
    index_expression = minic_c0_program_expression(parser->program, index_id);
    if (index_expression == NULL || !minic_type_is_integer(index_expression->type)) {
        minic_parser_error(parser, "array index must have integer type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RBRACKET) {
        minic_parser_error(parser, "expected ']'");
        return false;
    }
    subscript_end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&subscript, 0, sizeof(subscript));
    subscript.kind = MINIC_EXPRESSION_SUBSCRIPT;
    subscript.span.begin = base_span.begin;
    subscript.span.end = subscript_end;
    subscript.type = element_type;
    subscript.value_category = MINIC_VALUE_LVALUE;
    subscript.value.subscript.base = base_id;
    subscript.value.subscript.index = index_id;
    return minic_parser_add_expression(parser, &subscript, expression_id);
}

static const MinicFunctionType *indirect_callee_type(const MinicParser *parser,
                                                     MinicExpressionId callee_id) {
    const MinicExpression *callee;
    MinicType function_type;

    callee = minic_c0_program_expression(parser->program, callee_id);
    if (callee == NULL) {
        return NULL;
    }
    function_type = callee->type;
    if (!minic_type_is_function(function_type) &&
        (!minic_type_pointee(callee->type, &function_type) ||
         !minic_type_is_function(function_type))) {
        return NULL;
    }
    return minic_c0_program_function_type(parser->program, function_type.function_type_id);
}

static void indirect_argument_count_error(MinicParser *parser) {
    minic_parser_error(parser, "indirect call argument count does not match declaration");
}

static bool parse_indirect_arguments(MinicParser *parser,
                                     MinicExpression *call,
                                     const MinicFunctionType *function_type) {
    size_t argument_index;

    if (function_type == NULL || function_type->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        !minic_parser_advance(parser)) {
        return false;
    }
    for (argument_index = 0U; argument_index < function_type->parameter_count; ++argument_index) {
        const MinicExpression *argument;
        MinicExpressionId argument_id;

        if (parser->current.kind == MINIC_TOKEN_RPAREN ||
            !minic_parser_parse_expression(parser, &argument_id, 0U)) {
            indirect_argument_count_error(parser);
            return false;
        }
        argument = minic_c0_program_expression(parser->program, argument_id);
        if (argument == NULL ||
            !minic_parser_apply_fixed_call_argument_conversion(
                parser, function_type->parameter_types[argument_index], &argument_id) ||
            !minic_c0_assignment_compatible(
                parser->program, function_type->parameter_types[argument_index], argument_id)) {
            minic_parser_error(parser, "indirect call argument type does not match declaration");
            return false;
        }
        call->value.call.arguments[argument_index] = argument_id;
        if (argument_index + 1U < function_type->parameter_count) {
            if (parser->current.kind != MINIC_TOKEN_COMMA || !minic_parser_advance(parser)) {
                indirect_argument_count_error(parser);
                return false;
            }
        }
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        indirect_argument_count_error(parser);
        return false;
    }
    call->value.call.argument_count = function_type->parameter_count;
    return true;
}

static bool parse_one_indirect_call(MinicParser *parser,
                                    MinicExpressionId callee_id,
                                    MinicExpressionId *expression_id) {
    const MinicExpression *callee;
    const MinicFunctionType *function_type;
    MinicExpression call;
    MinicSourcePosition call_end;

    callee = minic_c0_program_expression(parser->program, callee_id);
    function_type = indirect_callee_type(parser, callee_id);
    if (callee == NULL || function_type == NULL) {
        minic_parser_error(parser, "called expression must have function-pointer type");
        return false;
    }

    (void)memset(&call, 0, sizeof(call));
    call.kind = MINIC_EXPRESSION_CALL;
    call.span.begin = callee->span.begin;
    call.type = function_type->return_type;
    call.value_category = MINIC_VALUE_RVALUE;
    call.value.call.function_id = MINIC_FUNCTION_INVALID;
    call.value.call.callee = callee_id;
    if (!parse_indirect_arguments(parser, &call, function_type)) {
        return false;
    }
    call_end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    call.span.end = call_end;
    return minic_parser_add_expression(parser, &call, expression_id);
}

static bool parse_one_postfix_update(MinicParser *parser,
                                     MinicExpressionId operand_id,
                                     MinicExpressionId *expression_id) {
    const MinicExpression *operand;
    MinicExpression update;
    MinicSourceSpan operand_span;
    MinicSourcePosition update_end;
    MinicType operand_type;
    MinicType array_element_type;
    MinicType pointee_type;
    MinicUnaryOperator operator_kind;

    operand = minic_c0_program_expression(parser->program, operand_id);
    if (operand == NULL) {
        minic_parser_error(parser, "invalid postfix update operand");
        return false;
    }
    operand_span = operand->span;
    operand_type = operand->type;
    if (operand->value_category != MINIC_VALUE_LVALUE || minic_type_is_const(operand_type) ||
        array_object_element_type(parser, operand_id, &array_element_type)) {
        minic_parser_error(parser, "postfix update requires a modifiable scalar lvalue");
        return false;
    }
    if (minic_type_is_pointer(operand_type)) {
        if (!minic_type_pointee(operand_type, &pointee_type) ||
            !minic_parser_require_complete_object_type(
                parser, pointee_type, "pointer update requires a complete object type")) {
            return false;
        }
    } else if (!minic_type_is_integer(operand_type)) {
        minic_parser_error(parser, "postfix update requires integer or pointer lvalue");
        return false;
    }

    operator_kind = parser->current.kind == MINIC_TOKEN_PLUS_PLUS ? MINIC_UNARY_POST_INCREMENT
                                                                  : MINIC_UNARY_POST_DECREMENT;
    update_end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    (void)memset(&update, 0, sizeof(update));
    update.kind = MINIC_EXPRESSION_UNARY;
    update.span.begin = operand_span.begin;
    update.span.end = update_end;
    update.type = operand_type;
    update.value_category = MINIC_VALUE_RVALUE;
    update.value.unary.operator_kind = operator_kind;
    update.value.unary.operand = operand_id;
    return minic_parser_add_expression(parser, &update, expression_id);
}

bool minic_parser_parse_postfix(MinicParser *parser,
                                MinicExpressionId base_id,
                                MinicExpressionId *expression_id) {
    MinicExpressionId current;

    current = base_id;
    for (;;) {
        if (parser->current.kind == MINIC_TOKEN_ARROW) {
            MinicExpressionId pointer_base;

            if (!minic_parser_apply_array_decay(parser, current, &pointer_base) ||
                !minic_parser_parse_pointer_member(parser, pointer_base, &current)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_DOT) {
            if (!minic_parser_parse_direct_member(parser, current, &current)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!parse_one_subscript(parser, current, &current)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            if (!parse_one_indirect_call(parser, current, &current)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_PLUS_PLUS ||
            parser->current.kind == MINIC_TOKEN_MINUS_MINUS) {
            if (!parse_one_postfix_update(parser, current, &current)) {
                return false;
            }
            continue;
        }
        break;
    }

    *expression_id = current;
    return true;
}
