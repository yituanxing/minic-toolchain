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
            if (callee == NULL || callee->parameter_count > 1U ||
                !minic_parser_advance(parser)) {
                minic_parser_error(parser, "unsupported function call signature");
                return false;
            }

            (void)memset(&expression, 0, sizeof(expression));
            expression.kind = MINIC_EXPRESSION_CALL;
            expression.span.begin = name_span.begin;
            expression.value.call.function_id = function_id;
            expression.value.call.argument_count = callee->parameter_count;
            if (callee->parameter_count == 1U) {
                if (parser->current.kind == MINIC_TOKEN_RPAREN) {
                    minic_parser_error(
                        parser,
                        "call argument count does not match declaration");
                    return false;
                }
                if (!minic_parser_parse_expression(
                        parser,
                        &expression.value.call.arguments[0],
                        0U)) {
                    return false;
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
            return minic_parser_add_expression(
                parser,
                &expression,
                expression_id);
        }

        if (local_id == MINIC_LOCAL_INVALID) {
            minic_parser_error(parser, "use of undeclared local");
            return false;
        }
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_LOCAL;
        expression.span = name_span;
        expression.value.local_id = local_id;
        return minic_parser_add_expression(parser, &expression, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        return minic_parser_advance(parser) &&
               minic_parser_parse_expression(parser, expression_id, 0U) &&
               minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'");
    }
    minic_parser_error(parser, "expected expression");
    return false;
}

static bool parse_unary(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicToken operator_token;
    MinicExpression expression;
    MinicExpressionId operand;
    const MinicExpression *operand_expression;

    if (parser->current.kind != MINIC_TOKEN_PLUS &&
        parser->current.kind != MINIC_TOKEN_MINUS &&
        parser->current.kind != MINIC_TOKEN_BANG) {
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
    expression.kind = MINIC_EXPRESSION_UNARY;
    expression.span.begin = operator_token.span.begin;
    expression.span.end = operand_expression->span.end;
    expression.value.unary.operand = operand;
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
            !minic_parser_parse_expression(
                parser,
                &right,
                precedence + 1U)) {
            return false;
        }
        left_expression = minic_c0_program_expression(parser->program, left);
        right_expression = minic_c0_program_expression(parser->program, right);
        if (left_expression == NULL || right_expression == NULL) {
            minic_parser_error(parser, "invalid binary operand");
            return false;
        }

        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_BINARY;
        expression.span.begin = left_expression->span.begin;
        expression.span.end = right_expression->span.end;
        expression.value.binary.operator_kind = binary_operator(token_kind);
        expression.value.binary.left = left;
        expression.value.binary.right = right;
        if (!minic_parser_add_expression(parser, &expression, &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}
