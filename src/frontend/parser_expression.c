#include "frontend/parser_internal.h"

#include <string.h>

static bool parse_unary(
    MinicParser *parser,
    MinicExpressionId *expression_id);

static bool parse_integer(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
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

static bool parse_local_reference(
    MinicParser *parser,
    MinicSourceSpan name_span,
    MinicLocalId local_id,
    MinicExpressionId *expression_id)
{
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
    return minic_parser_parse_postfix(
        parser,
        base_id,
        local->element_count > 1U,
        expression_id);
}

static bool parse_global_reference(
    MinicParser *parser,
    MinicSourceSpan name_span,
    MinicGlobalObjectId global_object_id,
    MinicExpressionId *expression_id)
{
    const MinicGlobalObject *object;
    MinicExpression base_expression;
    MinicExpressionId base_id;

    object = minic_c0_program_global_object(parser->program, global_object_id);
    if (object == NULL || !minic_type_is_array(object->type)) {
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
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "global array object requires a subscript");
        return false;
    }
    return minic_parser_parse_postfix(
        parser,
        base_id,
        true,
        expression_id);
}

static bool token_starts_cast_type(
    const MinicParser *parser,
    MinicToken token)
{
    switch (token.kind) {
    case MINIC_TOKEN_KW_CONST:
    case MINIC_TOKEN_KW_INT:
    case MINIC_TOKEN_KW_UNSIGNED:
    case MINIC_TOKEN_KW_VOID:
    case MINIC_TOKEN_KW_STRUCT:
        return true;
    case MINIC_TOKEN_IDENTIFIER:
        return minic_parser_find_local(parser, token.span) ==
                   MINIC_LOCAL_INVALID &&
               minic_parser_find_type_alias(parser, token.span) !=
                   MINIC_TYPE_ALIAS_INVALID;
    default:
        return false;
    }
}

static bool parenthesis_starts_cast(const MinicParser *parser)
{
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

static bool parse_cast(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicSourcePosition begin;
    MinicExpression expression;
    MinicExpressionId operand_id;
    const MinicExpression *operand;
    MinicType target_type;

    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_type_name(parser, &target_type) ||
        !minic_parser_expect(
            parser,
            MINIC_TOKEN_RPAREN,
            "expected ')' after cast type") ||
        !parse_unary(parser, &operand_id)) {
        return false;
    }

    operand = minic_c0_program_expression(parser->program, operand_id);
    if (operand == NULL ||
        !minic_type_cast_compatible(target_type, operand->type)) {
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

static bool parse_primary(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpression expression;
    MinicExpressionId primary_id;
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    MinicFunctionId function_id;
    MinicGlobalObjectId global_object_id;

    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return parse_integer(parser, &primary_id) &&
               minic_parser_parse_postfix(
                   parser,
                   primary_id,
                   false,
                   expression_id);
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
            if (callee == NULL || callee->parameter_count > 8U ||
                !minic_parser_advance(parser)) {
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

                for (argument_index = 0U;
                     argument_index < callee->parameter_count;
                     ++argument_index) {
                    const MinicExpression *argument;

                    if (parser->current.kind == MINIC_TOKEN_RPAREN) {
                        minic_parser_error(
                            parser,
                            "call argument count does not match declaration");
                        return false;
                    }
                    if (!minic_parser_parse_expression(
                            parser,
                            &expression.value.call.arguments[argument_index],
                            true)) {
                        return false;
                    }
                    argument = minic_c0_program_expression(
                        parser->program,
                        expression.value.call.arguments[argument_index]);
                    if (argument == NULL ||
                        !minic_type_assignment_compatible(
                            callee->parameter_types[argument_index],
                            argument->type)) {
                        minic_parser_error(
                            parser,
                            "call argument type does not match declaration");
                        return false;
                    }
                    if (argument_index + 1U < callee->parameter_count) {
                        if (!minic_parser_expect(
                                parser,
                                MINIC_TOKEN_COMMA,
                                "expected ',' between call arguments")) {
                            return false;
                        }
                    }
                }
            }
            call_end = parser->current.span.end;
            if (!minic_parser_expect(
                    parser,
                    MINIC_TOKEN_RPAREN,
                    "expected ')' after call arguments")) {
                return false;
            }
            expression.span.end = call_end;
            if (!minic_parser_add_expression(
                    parser,
                    &expression,
                    &primary_id)) {
                return false;
            }
            return minic_parser_parse_postfix(
                parser,
                primary_id,
                false,
                expression_id);
        }

        if (local_id != MINIC_LOCAL_INVALID) {
            return parse_local_reference(
                parser,
                name_span,
                local_id,
                expression_id);
        }
        if (global_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
            return parse_global_reference(
                parser,
                name_span,
                global_object_id,
                expression_id);
        }
        minic_parser_error(parser, "use of undeclared local");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (parenthesis_starts_cast(parser)) {
            return parse_cast(parser, expression_id);
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, expression_id, true)) {
            return false;
        }
        return minic_parser_expect(
            parser,
            MINIC_TOKEN_RPAREN,
            "expected ')' after expression");
    }
    minic_parser_error(parser, "expected expression");
    return false;
}

static bool parse_unary(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicUnaryOperator operator_kind;
    MinicSourcePosition begin;
    MinicExpressionId operand_id;
    const MinicExpression *operand;
    MinicExpression expression;

    if (parser->current.kind == MINIC_TOKEN_PLUS ||
        parser->current.kind == MINIC_TOKEN_MINUS ||
        parser->current.kind == MINIC_TOKEN_BANG) {
        begin = parser->current.span.begin;
        if (parser->current.kind == MINIC_TOKEN_PLUS) {
            operator_kind = MINIC_UNARY_PLUS;
        } else if (parser->current.kind == MINIC_TOKEN_MINUS) {
            operator_kind = MINIC_UNARY_NEGATE;
        } else {
            operator_kind = MINIC_UNARY_LOGICAL_NOT;
        }
        if (!minic_parser_advance(parser) ||
            !parse_unary(parser, &operand_id)) {
            return false;
        }
        operand = minic_c0_program_expression(parser->program, operand_id);
        if (operand == NULL || !minic_type_is_integer(operand->type)) {
            minic_parser_error(parser, "unary operator requires integer operand");
            return false;
        }
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_UNARY;
        expression.span.begin = begin;
        expression.span.end = operand->span.end;
        expression.type = operand->type;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.unary.operator_kind = operator_kind;
        expression.value.unary.operand = operand_id;
        return minic_parser_add_expression(parser, &expression, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_AMPERSAND) {
        begin = parser->current.span.begin;
        if (!minic_parser_advance(parser) ||
            !parse_unary(parser, &operand_id)) {
            return false;
        }
        operand = minic_c0_program_expression(parser->program, operand_id);
        if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE) {
            minic_parser_error(parser, "address-of requires an lvalue operand");
            return false;
        }
        if (!minic_type_pointer_to(operand->type, &expression.type)) {
            minic_parser_error(parser, "unsupported pointer depth");
            return false;
        }
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_ADDRESS_OF;
        expression.span.begin = begin;
        expression.span.end = operand->span.end;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.unary.operand = operand_id;
        if (!minic_type_pointer_to(operand->type, &expression.type)) {
            minic_parser_error(parser, "unsupported pointer depth");
            return false;
        }
        return minic_parser_add_expression(parser, &expression, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_STAR) {
        begin = parser->current.span.begin;
        if (!minic_parser_advance(parser) ||
            !parse_unary(parser, &operand_id)) {
            return false;
        }
        operand = minic_c0_program_expression(parser->program, operand_id);
        if (operand == NULL ||
            !minic_type_pointee(operand->type, &expression.type)) {
            minic_parser_error(parser, "dereference requires a pointer operand");
            return false;
        }
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_DEREFERENCE;
        expression.span.begin = begin;
        expression.span.end = operand->span.end;
        expression.value_category = MINIC_VALUE_LVALUE;
        expression.value.unary.operand = operand_id;
        if (!minic_type_pointee(operand->type, &expression.type)) {
            minic_parser_error(parser, "dereference requires a pointer operand");
            return false;
        }
        return minic_parser_parse_postfix(
            parser,
            minic_parser_add_expression(
                parser,
                &expression,
                expression_id)
                ? *expression_id
                : MINIC_EXPRESSION_INVALID,
            false,
            expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN &&
        parenthesis_starts_cast(parser)) {
        return parse_cast(parser, expression_id);
    }
    return parse_primary(parser, expression_id);
}

static bool build_binary_expression(
    MinicParser *parser,
    MinicBinaryOperator operator_kind,
    MinicExpressionId left_id,
    MinicExpressionId right_id,
    MinicExpressionId *expression_id)
{
    const MinicExpression *left;
    const MinicExpression *right;
    MinicExpression expression;
    MinicType common_type;

    left = minic_c0_program_expression(parser->program, left_id);
    right = minic_c0_program_expression(parser->program, right_id);
    if (left == NULL || right == NULL) {
        minic_parser_error(parser, "invalid binary expression operands");
        return false;
    }
    if (minic_type_is_integer(left->type) &&
        minic_type_is_integer(right->type)) {
        if (!minic_type_integer_common(left->type, right->type, &common_type)) {
            minic_parser_error(parser, "unsupported integer conversion");
            return false;
        }
        expression.type = common_type;
    } else if ((operator_kind == MINIC_BINARY_ADD ||
                operator_kind == MINIC_BINARY_SUBTRACT) &&
               minic_type_is_pointer(left->type) &&
               minic_type_is_integer(right->type)) {
        expression.type = left->type;
    } else if (operator_kind == MINIC_BINARY_ADD &&
               minic_type_is_integer(left->type) &&
               minic_type_is_pointer(right->type)) {
        expression.type = right->type;
    } else {
        minic_parser_error(parser, "unsupported pointer arithmetic operands");
        return false;
    }

    (void)memset(&expression.value, 0, sizeof(expression.value));
    expression.kind = MINIC_EXPRESSION_BINARY;
    expression.span.begin = left->span.begin;
    expression.span.end = right->span.end;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.binary.operator_kind = operator_kind;
    expression.value.binary.left = left_id;
    expression.value.binary.right = right_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool build_comparison_expression(
    MinicParser *parser,
    MinicBinaryOperator operator_kind,
    MinicExpressionId left_id,
    MinicExpressionId right_id,
    MinicExpressionId *expression_id)
{
    const MinicExpression *left;
    const MinicExpression *right;
    MinicExpression expression;
    MinicType common_type;

    left = minic_c0_program_expression(parser->program, left_id);
    right = minic_c0_program_expression(parser->program, right_id);
    if (left == NULL || right == NULL ||
        !minic_type_integer_common(left->type, right->type, &common_type)) {
        minic_parser_error(parser, "comparison requires integer operands");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BINARY;
    expression.span.begin = left->span.begin;
    expression.span.end = right->span.end;
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.binary.operator_kind = operator_kind;
    expression.value.binary.left = left_id;
    expression.value.binary.right = right_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_multiplicative(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left_id;

    if (!parse_unary(parser, &left_id)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR ||
           parser->current.kind == MINIC_TOKEN_SLASH ||
           parser->current.kind == MINIC_TOKEN_PERCENT) {
        MinicTokenKind token_kind;
        MinicExpressionId right_id;
        MinicBinaryOperator operator_kind;

        token_kind = parser->current.kind;
        if (!minic_parser_advance(parser) ||
            !parse_unary(parser, &right_id)) {
            return false;
        }
        if (token_kind == MINIC_TOKEN_STAR) {
            operator_kind = MINIC_BINARY_MULTIPLY;
        } else if (token_kind == MINIC_TOKEN_SLASH) {
            operator_kind = MINIC_BINARY_DIVIDE;
        } else {
            operator_kind = MINIC_BINARY_REMAINDER;
        }
        if (!build_binary_expression(
                parser,
                operator_kind,
                left_id,
                right_id,
                &left_id)) {
            return false;
        }
    }
    *expression_id = left_id;
    return true;
}

static bool parse_additive(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left_id;

    if (!parse_multiplicative(parser, &left_id)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_PLUS ||
           parser->current.kind == MINIC_TOKEN_MINUS) {
        MinicTokenKind token_kind;
        MinicExpressionId right_id;
        MinicBinaryOperator operator_kind;

        token_kind = parser->current.kind;
        if (!minic_parser_advance(parser) ||
            !parse_multiplicative(parser, &right_id)) {
            return false;
        }
        operator_kind = token_kind == MINIC_TOKEN_PLUS
            ? MINIC_BINARY_ADD
            : MINIC_BINARY_SUBTRACT;
        if (!build_binary_expression(
                parser,
                operator_kind,
                left_id,
                right_id,
                &left_id)) {
            return false;
        }
    }
    *expression_id = left_id;
    return true;
}

static bool parse_shift(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left_id;

    if (!parse_additive(parser, &left_id)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_LESS_LESS ||
           parser->current.kind == MINIC_TOKEN_GREATER_GREATER) {
        MinicTokenKind token_kind;
        MinicExpressionId right_id;
        const MinicExpression *left;
        const MinicExpression *right;
        MinicExpression expression;
        MinicType promoted_left;
        MinicType promoted_right;

        token_kind = parser->current.kind;
        if (!minic_parser_advance(parser) ||
            !parse_additive(parser, &right_id)) {
            return false;
        }
        left = minic_c0_program_expression(parser->program, left_id);
        right = minic_c0_program_expression(parser->program, right_id);
        if (left == NULL || right == NULL ||
            !minic_type_integer_promotion(left->type, &promoted_left) ||
            !minic_type_integer_promotion(right->type, &promoted_right)) {
            minic_parser_error(parser, "shift operator requires integer operands");
            return false;
        }
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_BINARY;
        expression.span.begin = left->span.begin;
        expression.span.end = right->span.end;
        expression.type = promoted_left;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.binary.operator_kind =
            token_kind == MINIC_TOKEN_LESS_LESS
                ? MINIC_BINARY_SHIFT_LEFT
                : MINIC_BINARY_SHIFT_RIGHT;
        expression.value.binary.left = left_id;
        expression.value.binary.right = right_id;
        if (!minic_parser_add_expression(
                parser,
                &expression,
                &left_id)) {
            return false;
        }
    }
    *expression_id = left_id;
    return true;
}

static bool parse_relational(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left_id;

    if (!parse_shift(parser, &left_id)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_LESS ||
           parser->current.kind == MINIC_TOKEN_LESS_EQUAL ||
           parser->current.kind == MINIC_TOKEN_GREATER ||
           parser->current.kind == MINIC_TOKEN_GREATER_EQUAL) {
        MinicTokenKind token_kind;
        MinicExpressionId right_id;
        MinicBinaryOperator operator_kind;

        token_kind = parser->current.kind;
        if (!minic_parser_advance(parser) ||
            !parse_shift(parser, &right_id)) {
            return false;
        }
        switch (token_kind) {
        case MINIC_TOKEN_LESS:
            operator_kind = MINIC_BINARY_LESS;
            break;
        case MINIC_TOKEN_LESS_EQUAL:
            operator_kind = MINIC_BINARY_LESS_EQUAL;
            break;
        case MINIC_TOKEN_GREATER:
            operator_kind = MINIC_BINARY_GREATER;
            break;
        case MINIC_TOKEN_GREATER_EQUAL:
            operator_kind = MINIC_BINARY_GREATER_EQUAL;
            break;
        default:
            return false;
        }
        if (!build_comparison_expression(
                parser,
                operator_kind,
                left_id,
                right_id,
                &left_id)) {
            return false;
        }
    }
    *expression_id = left_id;
    return true;
}

static bool parse_equality(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left_id;

    if (!parse_relational(parser, &left_id)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_EQUAL_EQUAL ||
           parser->current.kind == MINIC_TOKEN_BANG_EQUAL) {
        MinicTokenKind token_kind;
        MinicExpressionId right_id;
        MinicBinaryOperator operator_kind;

        token_kind = parser->current.kind;
        if (!minic_parser_advance(parser) ||
            !parse_relational(parser, &right_id)) {
            return false;
        }
        operator_kind = token_kind == MINIC_TOKEN_EQUAL_EQUAL
            ? MINIC_BINARY_EQUAL
            : MINIC_BINARY_NOT_EQUAL;
        if (!build_comparison_expression(
                parser,
                operator_kind,
                left_id,
                right_id,
                &left_id)) {
            return false;
        }
    }
    *expression_id = left_id;
    return true;
}

static bool parse_bitwise_and(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left_id;

    if (!parse_equality(parser, &left_id)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_AMPERSAND) {
        MinicExpressionId right_id;

        if (!minic_parser_advance(parser) ||
            !parse_equality(parser, &right_id) ||
            !build_binary_expression(
                parser,
                MINIC_BINARY_BITWISE_AND,
                left_id,
                right_id,
                &left_id)) {
            return false;
        }
    }
    *expression_id = left_id;
    return true;
}

static bool parse_bitwise_xor(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left_id;

    if (!parse_bitwise_and(parser, &left_id)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_CARET) {
        MinicExpressionId right_id;

        if (!minic_parser_advance(parser) ||
            !parse_bitwise_and(parser, &right_id) ||
            !build_binary_expression(
                parser,
                MINIC_BINARY_BITWISE_XOR,
                left_id,
                right_id,
                &left_id)) {
            return false;
        }
    }
    *expression_id = left_id;
    return true;
}

bool minic_parser_parse_expression(
    MinicParser *parser,
    MinicExpressionId *expression_id,
    bool allow_array_result)
{
    const MinicExpression *expression;

    if (!parse_bitwise_xor(parser, expression_id)) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, *expression_id);
    if (expression == NULL) {
        minic_parser_error(parser, "invalid expression result");
        return false;
    }
    if (!allow_array_result &&
        expression->value_category == MINIC_VALUE_LVALUE &&
        minic_type_is_array(expression->type)) {
        minic_parser_error(parser, "array object requires a subscript");
        return false;
    }
    return true;
}
