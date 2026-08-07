#include "frontend/parser_internal.h"

#include <limits.h>
#include <string.h>

static bool parse_unary(MinicParser *parser, MinicExpressionId *expression_id);

static bool parse_integer(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    int value;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = parser->current.span;
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    if (!minic_parser_parse_integer_value(parser, &value)) {
        return false;
    }
    expression.value.integer_value = value;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_local_reference(MinicParser *parser,
                                  MinicSourceSpan name_span,
                                  MinicLocalId local_id,
                                  MinicExpressionId *expression_id) {
    const MinicLocal *local;
    MinicExpression base_expression;
    MinicExpressionId base_id;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL) {
        minic_parser_error(parser, "invalid local reference");
        return false;
    }

    (void)memset(&base_expression, 0, sizeof(base_expression));
    base_expression.kind = MINIC_EXPRESSION_LOCAL;
    base_expression.span = name_span;
    base_expression.type = local->type;
    base_expression.value_category = MINIC_VALUE_LVALUE;
    base_expression.value.local_id = local_id;
    if (!minic_parser_add_expression(parser, &base_expression, &base_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, base_id, local->element_count > 1U, expression_id);
}

static bool parse_global_reference(MinicParser *parser,
                                   MinicSourceSpan name_span,
                                   MinicGlobalObjectId global_object_id,
                                   MinicExpressionId *expression_id) {
    const MinicGlobalObject *object;
    MinicExpression base_expression;
    MinicExpressionId base_id;
    bool require_subscript;

    object = minic_c0_program_global_object(parser->program, global_object_id);
    if (object == NULL) {
        minic_parser_error(parser, "invalid global object reference");
        return false;
    }
    require_subscript = false;
    if (minic_type_is_array(object->type)) {
        if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
            minic_parser_error(parser, "global array object requires a subscript");
            return false;
        }
        require_subscript = true;
    } else if (minic_type_is_record(object->type)) {
        if (parser->current.kind != MINIC_TOKEN_DOT) {
            minic_parser_error(parser, "global record object requires member access");
            return false;
        }
    } else {
        minic_parser_error(parser, "invalid global object reference");
        return false;
    }

    (void)memset(&base_expression, 0, sizeof(base_expression));
    base_expression.kind = MINIC_EXPRESSION_GLOBAL_OBJECT;
    base_expression.span = name_span;
    base_expression.type = object->type;
    base_expression.value_category = MINIC_VALUE_LVALUE;
    base_expression.value.global_object_id = global_object_id;
    if (!minic_parser_add_expression(parser, &base_expression, &base_id)) {
        return false;
    }
    return minic_parser_parse_postfix(parser, base_id, require_subscript, expression_id);
}

static bool token_starts_cast_type(const MinicParser *parser, MinicToken token) {
    switch (token.kind) {
    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_CHAR:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_LONG:
    case MINIC_TOKEN_KW_SIGNED:
    case MINIC_TOKEN_KW_UNSIGNED:
    case MINIC_TOKEN_KW_FLOAT:
    case MINIC_TOKEN_KW_DOUBLE:
    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
    case MINIC_TOKEN_IDENTIFIER:
        return minic_parser_find_local(parser, token.span) == MINIC_LOCAL_INVALID &&
               minic_parser_find_type_alias(parser, token.span) != MINIC_TYPE_ALIAS_INVALID;
    default:
        return false;
    }
}

static bool parenthesis_starts_cast(const MinicParser *parser) {
    MinicDiagnostic diagnostic;
    MinicLexer lookahead;
    MinicToken token;

    if (parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }

    lookahead = parser->lexer;
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    if (!minic_lexer_next(&lookahead, &token, &diagnostic)) {
        return false;
    }
    return token_starts_cast_type(parser, token);
}

static bool expression_is_integer_zero(const MinicExpression *expression) {
    return expression != NULL && expression->kind == MINIC_EXPRESSION_INTEGER &&
           minic_type_is_integer(expression->type) && expression->value.integer_value == 0;
}

static bool parse_cast(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourcePosition begin;
    MinicExpression expression;
    MinicExpressionId operand_id;
    const MinicExpression *operand;
    MinicType target_type;

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) || !minic_parser_parse_type_name(parser, &target_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after cast type") ||
        !parse_unary(parser, &operand_id)) {
        return false;
    }

    operand = minic_c0_program_expression(parser->program, operand_id);
    if (operand == NULL ||
        (!minic_type_cast_compatible(target_type, operand->type) &&
         !(minic_type_is_pointer(target_type) && expression_is_integer_zero(operand)))) {
        minic_parser_error(parser, "unsupported cast between these types");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_CAST;
    expression.span.begin = begin;
    expression.span.end = operand->span.end;
    expression.type = target_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = operand_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicExpressionId primary_id;
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    MinicFunctionId function_id;
    MinicGlobalObjectId global_object_id;

    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return parse_integer(parser, &primary_id) &&
               minic_parser_parse_postfix(parser, primary_id, false, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
        local_id = minic_parser_find_local(parser, name_span);
        function_id = minic_parser_find_function(parser, name_span);
        global_object_id = minic_parser_find_global_object(parser, name_span);
        if (!minic_parser_advance(parser)) {
            return false;
        }

        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            MinicSourcePosition call_end;
            const MinicFunction *callee;

            if (local_id != MINIC_LOCAL_INVALID) {
                minic_parser_error(parser, "called object is a local variable");
                return false;
            }
            if (global_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
                minic_parser_error(parser, "called object is a global object");
                return false;
            }
            if (function_id == MINIC_FUNCTION_INVALID) {
                minic_parser_error(parser, "call to function not yet declared");
                return false;
            }
            callee = minic_c0_program_function(parser->program, function_id);
            if (callee == NULL || callee->parameter_count > 8U || !minic_parser_advance(parser)) {
                minic_parser_error(parser, "unsupported function call signature");
                return false;
            }

            (void)memset(&expression, 0, sizeof(expression));
            expression.kind = MINIC_EXPRESSION_CALL;
            expression.span.begin = name_span.begin;
            expression.type = callee->return_type;
            expression.value_category = MINIC_VALUE_RVALUE;
            expression.value.call.function_id = function_id;
            expression.value.call.argument_count = callee->parameter_count;
            {
                size_t argument_index;

                for (argument_index = 0U; argument_index < callee->parameter_count;
                     ++argument_index) {
                    const MinicExpression *argument;

                    if (parser->current.kind == MINIC_TOKEN_RPAREN) {
                        minic_parser_error(parser,
                                           "call argument count does not match declaration");
                        return false;
                    }
                    if (!minic_parser_parse_expression(
                            parser, &expression.value.call.arguments[argument_index], 0U)) {
                        return false;
                    }
                    argument = minic_c0_program_expression(
                        parser->program, expression.value.call.arguments[argument_index]);
                    if (argument == NULL ||
                        !minic_type_assignment_compatible(callee->parameter_types[argument_index],
                                                          argument->type)) {
                        minic_parser_error(parser, "call argument type does not match declaration");
                        return false;
                    }
                    if (argument_index + 1U < callee->parameter_count) {
                        if (parser->current.kind != MINIC_TOKEN_COMMA) {
                            minic_parser_error(parser,
                                               "call argument count does not match declaration");
                            return false;
                        }
                        if (!minic_parser_advance(parser)) {
                            return false;
                        }
                    }
                }
            }
            if (parser->current.kind != MINIC_TOKEN_RPAREN) {
                minic_parser_error(parser, "call argument count does not match declaration");
                return false;
            }
            call_end = parser->current.span.end;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            expression.span.end = call_end;
            if (!minic_parser_add_expression(parser, &expression, &primary_id)) {
                return false;
            }
            return minic_parser_parse_postfix(parser, primary_id, false, expression_id);
        }

        if (local_id != MINIC_LOCAL_INVALID) {
            return parse_local_reference(parser, name_span, local_id, expression_id);
        }
        if (global_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
            return parse_global_reference(parser, name_span, global_object_id, expression_id);
        }
        minic_parser_error(parser, "use of undeclared local");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &primary_id, 0U) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
            return false;
        }
        return minic_parser_parse_postfix(parser, primary_id, false, expression_id);
    }
    minic_parser_error(parser, "expected expression");
    return false;
}

static bool parse_unary(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicToken operator_token;
    MinicExpression expression;
    MinicExpressionId operand;
    const MinicExpression *operand_expression;

    if (parenthesis_starts_cast(parser)) {
        return parse_cast(parser, expression_id);
    }
    if (parser->current.kind != MINIC_TOKEN_PLUS && parser->current.kind != MINIC_TOKEN_MINUS &&
        parser->current.kind != MINIC_TOKEN_BANG && parser->current.kind != MINIC_TOKEN_AMPERSAND &&
        parser->current.kind != MINIC_TOKEN_STAR) {
        return parse_primary(parser, expression_id);
    }

    operator_token = parser->current;
    if (!minic_parser_advance(parser) || !parse_unary(parser, &operand)) {
        return false;
    }
    operand_expression = minic_c0_program_expression(parser->program, operand);
    if (operand_expression == NULL) {
        minic_parser_error(parser, "invalid unary operand");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.span.begin = operator_token.span.begin;
    expression.span.end = operand_expression->span.end;
    expression.value.unary.operand = operand;

    if (operator_token.kind == MINIC_TOKEN_AMPERSAND) {
        if (operand_expression->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_pointer_to(operand_expression->type, &expression.type)) {
            minic_parser_error(parser, "address-of requires an lvalue operand");
            return false;
        }
        expression.kind = MINIC_EXPRESSION_ADDRESS_OF;
        expression.value_category = MINIC_VALUE_RVALUE;
        return minic_parser_add_expression(parser, &expression, expression_id);
    }

    if (operator_token.kind == MINIC_TOKEN_STAR) {
        if (!minic_type_pointee(operand_expression->type, &expression.type)) {
            minic_parser_error(parser, "dereference requires a pointer operand");
            return false;
        }
        expression.kind = MINIC_EXPRESSION_DEREFERENCE;
        expression.value_category = MINIC_VALUE_LVALUE;
        return minic_parser_add_expression(parser, &expression, expression_id);
    }

    if (!minic_type_is_integer(operand_expression->type)) {
        minic_parser_error(parser, "unary operator requires an int operand");
        return false;
    }
    expression.kind = MINIC_EXPRESSION_UNARY;
    expression.value_category = MINIC_VALUE_RVALUE;
    if (operator_token.kind == MINIC_TOKEN_PLUS) {
        expression.value.unary.operator_kind = MINIC_UNARY_PLUS;
        if (!minic_type_integer_common(
                operand_expression->type, operand_expression->type, &expression.type)) {
            return false;
        }
    } else if (operator_token.kind == MINIC_TOKEN_MINUS) {
        expression.value.unary.operator_kind = MINIC_UNARY_NEGATE;
        if (!minic_type_integer_common(
                operand_expression->type, operand_expression->type, &expression.type)) {
            return false;
        }
    } else {
        expression.value.unary.operator_kind = MINIC_UNARY_LOGICAL_NOT;
        expression.type = minic_type_int();
    }
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static unsigned int binary_precedence(MinicTokenKind kind) {
    switch (kind) {
    case MINIC_TOKEN_STAR:
    case MINIC_TOKEN_SLASH:
    case MINIC_TOKEN_PERCENT:
        return 70U;
    case MINIC_TOKEN_PLUS:
    case MINIC_TOKEN_MINUS:
        return 60U;
    case MINIC_TOKEN_LESS_LESS:
    case MINIC_TOKEN_GREATER_GREATER:
        return 50U;
    case MINIC_TOKEN_LESS:
    case MINIC_TOKEN_LESS_EQUAL:
    case MINIC_TOKEN_GREATER:
    case MINIC_TOKEN_GREATER_EQUAL:
        return 40U;
    case MINIC_TOKEN_EQUAL_EQUAL:
    case MINIC_TOKEN_BANG_EQUAL:
        return 30U;
    case MINIC_TOKEN_AMPERSAND:
        return 20U;
    case MINIC_TOKEN_CARET:
        return 10U;
    default:
        return 0U;
    }
}

static MinicBinaryOperator binary_operator(MinicTokenKind kind) {
    switch (kind) {
    case MINIC_TOKEN_PLUS:
        return MINIC_BINARY_ADD;
    case MINIC_TOKEN_MINUS:
        return MINIC_BINARY_SUBTRACT;
    case MINIC_TOKEN_STAR:
        return MINIC_BINARY_MULTIPLY;
    case MINIC_TOKEN_SLASH:
        return MINIC_BINARY_DIVIDE;
    case MINIC_TOKEN_PERCENT:
        return MINIC_BINARY_REMAINDER;
    case MINIC_TOKEN_LESS_LESS:
        return MINIC_BINARY_SHIFT_LEFT;
    case MINIC_TOKEN_GREATER_GREATER:
        return MINIC_BINARY_SHIFT_RIGHT;
    case MINIC_TOKEN_AMPERSAND:
        return MINIC_BINARY_BITWISE_AND;
    case MINIC_TOKEN_CARET:
        return MINIC_BINARY_BITWISE_XOR;
    case MINIC_TOKEN_EQUAL_EQUAL:
        return MINIC_BINARY_EQUAL;
    case MINIC_TOKEN_BANG_EQUAL:
        return MINIC_BINARY_NOT_EQUAL;
    case MINIC_TOKEN_LESS:
        return MINIC_BINARY_LESS;
    case MINIC_TOKEN_LESS_EQUAL:
        return MINIC_BINARY_LESS_EQUAL;
    case MINIC_TOKEN_GREATER:
        return MINIC_BINARY_GREATER;
    case MINIC_TOKEN_GREATER_EQUAL:
        return MINIC_BINARY_GREATER_EQUAL;
    default:
        return MINIC_BINARY_ADD;
    }
}

static bool binary_is_comparison(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_EQUAL_EQUAL || kind == MINIC_TOKEN_BANG_EQUAL ||
           kind == MINIC_TOKEN_LESS || kind == MINIC_TOKEN_LESS_EQUAL ||
           kind == MINIC_TOKEN_GREATER || kind == MINIC_TOKEN_GREATER_EQUAL;
}

static bool binary_is_shift(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_LESS_LESS || kind == MINIC_TOKEN_GREATER_GREATER;
}

static bool type_is_complete_object(const MinicC0Program *program, MinicType type) {
    if (program == NULL || minic_type_is_void(type)) {
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

        array_type = minic_c0_program_array_type(program, type.array_type_id);
        return array_type != NULL && array_type->element_count != 0U &&
               type_is_complete_object(program, array_type->element_type);
    }
    return false;
}

static bool pointer_arithmetic_shape(MinicTokenKind kind,
                                     MinicType left,
                                     MinicType right,
                                     MinicType *pointer_type) {
    if (pointer_type == NULL) {
        return false;
    }
    if (kind == MINIC_TOKEN_PLUS) {
        if (minic_type_is_pointer(left) && minic_type_is_integer(right)) {
            *pointer_type = left;
            return true;
        }
        if (minic_type_is_integer(left) && minic_type_is_pointer(right)) {
            *pointer_type = right;
            return true;
        }
        return false;
    }
    if (kind == MINIC_TOKEN_MINUS && minic_type_is_pointer(left) && minic_type_is_integer(right)) {
        *pointer_type = left;
        return true;
    }
    return false;
}

static bool binary_result_type(const MinicC0Program *program,
                               MinicTokenKind kind,
                               MinicType left,
                               MinicType right,
                               MinicType *result) {
    MinicType pointer_type;
    MinicType pointee_type;

    if (result == NULL) {
        return false;
    }
    if (minic_type_is_integer(left) && minic_type_is_integer(right)) {
        if (binary_is_comparison(kind)) {
            *result = minic_type_int();
            return true;
        }
        if (binary_is_shift(kind)) {
            return minic_type_integer_common(left, left, result);
        }
        return minic_type_integer_common(left, right, result);
    }
    if (!pointer_arithmetic_shape(kind, left, right, &pointer_type) ||
        !minic_type_pointee(pointer_type, &pointee_type) ||
        !type_is_complete_object(program, pointee_type)) {
        return false;
    }
    *result = pointer_type;
    return true;
}

bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence) {
    MinicExpressionId left;

    if (!parse_unary(parser, &left)) {
        return false;
    }
    for (;;) {
        MinicTokenKind token_kind;
        unsigned int precedence;
        MinicExpressionId right;
        MinicExpression expression;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        token_kind = parser->current.kind;
        precedence = binary_precedence(token_kind);
        if (precedence == 0U || precedence < minimum_precedence) {
            break;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &right, precedence + 1U)) {
            return false;
        }
        left_expression = minic_c0_program_expression(parser->program, left);
        right_expression = minic_c0_program_expression(parser->program, right);
        if (left_expression == NULL || right_expression == NULL) {
            minic_parser_error(parser, "invalid binary operands");
            return false;
        }

        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_BINARY;
        expression.span.begin = left_expression->span.begin;
        expression.span.end = right_expression->span.end;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.binary.operator_kind = binary_operator(token_kind);
        expression.value.binary.left = left;
        expression.value.binary.right = right;
        if (!binary_result_type(parser->program,
                                token_kind,
                                left_expression->type,
                                right_expression->type,
                                &expression.type)) {
            MinicType pointer_type;
            MinicType pointee_type;

            if (pointer_arithmetic_shape(
                    token_kind, left_expression->type, right_expression->type, &pointer_type) &&
                minic_type_pointee(pointer_type, &pointee_type) &&
                !type_is_complete_object(parser->program, pointee_type)) {
                minic_parser_error(parser, "pointer arithmetic requires a complete object type");
            } else if (token_kind == MINIC_TOKEN_PLUS || token_kind == MINIC_TOKEN_MINUS) {
                minic_parser_error(parser, "unsupported pointer arithmetic operands");
            } else {
                minic_parser_error(parser, "binary operator requires int operands");
            }
            return false;
        }
        if (!minic_parser_add_expression(parser, &expression, &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}
