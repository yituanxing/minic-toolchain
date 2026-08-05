#include "frontend/parser_internal.h"

#include <limits.h>
#include <string.h>

static bool parse_integer(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpression expression;
    size_t offset;
    unsigned long value;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = parser->current.span;
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    value = 0UL;
    for (offset = expression.span.begin.offset;
         offset < expression.span.end.offset;
         ++offset) {
        unsigned long digit;

        digit = (unsigned long)(unsigned int)(parser->source[offset] - '0');
        if (value > ((unsigned long)INT_MAX - digit) / 10UL) {
            minic_parser_error(parser, "integer constant exceeds C0 int range");
            return false;
        }
        value = value * 10UL + digit;
    }
    expression.value.integer_value = (int)value;
    return minic_parser_add_expression(parser, &expression, expression_id) &&
           minic_parser_advance(parser);
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

    if (local->element_count == 1U) {
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            minic_parser_error(parser, "subscript base must be an array object");
            return false;
        }
        *expression_id = base_id;
        return true;
    }

    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "array object requires a subscript");
        return false;
    }
    {
        MinicExpression subscript;
        MinicExpressionId index_id;
        const MinicExpression *index_expression;
        MinicSourcePosition subscript_end;

        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &index_id, 0U)) {
            return false;
        }
        index_expression = minic_c0_program_expression(
            parser->program,
            index_id);
        if (index_expression == NULL ||
            !minic_type_is_integer(index_expression->type)) {
            minic_parser_error(parser, "array index must have int type");
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
        subscript.span.begin = name_span.begin;
        subscript.span.end = subscript_end;
        subscript.type = local->type;
        subscript.value_category = MINIC_VALUE_LVALUE;
        subscript.value.subscript.base = base_id;
        subscript.value.subscript.index = index_id;
        return minic_parser_add_expression(
            parser,
            &subscript,
            expression_id);
    }
}

static bool parse_primary(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpression expression;
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    MinicFunctionId function_id;

    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return parse_integer(parser, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
        local_id = minic_parser_find_local(parser, name_span);
        function_id = minic_parser_find_function(parser, name_span);
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
            expression.type = minic_type_int();
            expression.value_category = MINIC_VALUE_RVALUE;
            expression.value.call.function_id = function_id;
            expression.value.call.argument_count = callee->parameter_count;
            {
                size_t argument_index;

                for (argument_index = 0U;
                     argument_index < callee->parameter_count;
                     ++argument_index) {
                    if (parser->current.kind == MINIC_TOKEN_RPAREN) {
                        minic_parser_error(
                            parser,
                            "call argument count does not match declaration");
                        return false;
                    }
                    if (!minic_parser_parse_expression(
                            parser,
                            &expression.value.call.arguments[argument_index],
                            0U)) {
                        return false;
                    }
                    if (argument_index + 1U < callee->parameter_count) {
                        if (parser->current.kind != MINIC_TOKEN_COMMA) {
                            minic_parser_error(
                                parser,
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
                minic_parser_error(
                    parser,
                    "call argument count does not match declaration");
                return false;
            }
            call_end = parser->current.span.end;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            expression.span.end = call_end;
            return minic_parser_add_expression(parser, &expression, expression_id);
        }

        if (local_id == MINIC_LOCAL_INVALID) {
            minic_parser_error(parser, "use of undeclared local");
            return false;
        }
        return parse_local_reference(
            parser,
            name_span,
            local_id,
            expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        return minic_parser_advance(parser) &&
               minic_parser_parse_expression(parser, expression_id, 0U) &&
               minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'");
    }
    minic_parser_error(parser, "expected expression");
    return false;
}

static bool parse_unary(MinicParser *parser, MinicExpressionId *expression_id)
{
    MinicToken operator_token;
    MinicExpression expression;
    MinicExpressionId operand;
    const MinicExpression *operand_expression;

    if (parser->current.kind != MINIC_TOKEN_PLUS &&
        parser->current.kind != MINIC_TOKEN_MINUS &&
        parser->current.kind != MINIC_TOKEN_BANG &&
        parser->current.kind != MINIC_TOKEN_AMPERSAND &&
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
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    if (operator_token.kind == MINIC_TOKEN_PLUS) {
        expression.value.unary.operator_kind = MINIC_UNARY_PLUS;
    } else if (operator_token.kind == MINIC_TOKEN_MINUS) {
        expression.value.unary.operator_kind = MINIC_UNARY_NEGATE;
    } else {
        expression.value.unary.operator_kind = MINIC_UNARY_LOGICAL_NOT;
    }
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static unsigned int binary_precedence(MinicTokenKind kind)
{
    switch (kind) {
    case MINIC_TOKEN_STAR:
    case MINIC_TOKEN_SLASH:
    case MINIC_TOKEN_PERCENT:
        return 50U;
    case MINIC_TOKEN_PLUS:
    case MINIC_TOKEN_MINUS:
        return 40U;
    case MINIC_TOKEN_LESS:
    case MINIC_TOKEN_LESS_EQUAL:
    case MINIC_TOKEN_GREATER:
    case MINIC_TOKEN_GREATER_EQUAL:
        return 30U;
    case MINIC_TOKEN_EQUAL_EQUAL:
    case MINIC_TOKEN_BANG_EQUAL:
        return 20U;
    default:
        return 0U;
    }
}

static MinicBinaryOperator binary_operator(MinicTokenKind kind)
{
    switch (kind) {
    case MINIC_TOKEN_PLUS: return MINIC_BINARY_ADD;
    case MINIC_TOKEN_MINUS: return MINIC_BINARY_SUBTRACT;
    case MINIC_TOKEN_STAR: return MINIC_BINARY_MULTIPLY;
    case MINIC_TOKEN_SLASH: return MINIC_BINARY_DIVIDE;
    case MINIC_TOKEN_PERCENT: return MINIC_BINARY_REMAINDER;
    case MINIC_TOKEN_EQUAL_EQUAL: return MINIC_BINARY_EQUAL;
    case MINIC_TOKEN_BANG_EQUAL: return MINIC_BINARY_NOT_EQUAL;
    case MINIC_TOKEN_LESS: return MINIC_BINARY_LESS;
    case MINIC_TOKEN_LESS_EQUAL: return MINIC_BINARY_LESS_EQUAL;
    case MINIC_TOKEN_GREATER: return MINIC_BINARY_GREATER;
    case MINIC_TOKEN_GREATER_EQUAL: return MINIC_BINARY_GREATER_EQUAL;
    default: return MINIC_BINARY_ADD;
    }
}

static bool binary_result_type(
    MinicTokenKind kind,
    MinicType left,
    MinicType right,
    MinicType *result)
{
    if (result == NULL) {
        return false;
    }
    if (minic_type_is_integer(left) && minic_type_is_integer(right)) {
        *result = minic_type_int();
        return true;
    }
    if (kind == MINIC_TOKEN_PLUS) {
        if (minic_type_is_pointer(left) && minic_type_is_integer(right)) {
            *result = left;
            return true;
        }
        if (minic_type_is_integer(left) && minic_type_is_pointer(right)) {
            *result = right;
            return true;
        }
        return false;
    }
    if (kind == MINIC_TOKEN_MINUS &&
        minic_type_is_pointer(left) &&
        minic_type_is_integer(right)) {
        *result = left;
        return true;
    }
    return false;
}

bool minic_parser_parse_expression(
    MinicParser *parser,
    MinicExpressionId *expression_id,
    unsigned int minimum_precedence)
{
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
        if (!binary_result_type(
                token_kind,
                left_expression->type,
                right_expression->type,
                &expression.type)) {
            if (token_kind == MINIC_TOKEN_PLUS ||
                token_kind == MINIC_TOKEN_MINUS) {
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
