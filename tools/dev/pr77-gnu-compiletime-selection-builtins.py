#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_expression.c")
text = path.read_text()
marker = "static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {\n"
if text.count(marker) != 1:
    raise SystemExit(f"GNU selection builtins: expected one parse_primary marker, found {text.count(marker)}")

helpers = r'''static bool builtin_constant_integer_value(const MinicC0Program *program,
                                           MinicExpressionId expression_id,
                                           int64_t *value) {
    const MinicExpression *expression;

    if (program == NULL || value == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        *value = expression->value.integer_value;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY) {
        int64_t operand;

        if (!builtin_constant_integer_value(program, expression->value.unary.operand, &operand)) {
            return false;
        }
        switch (expression->value.unary.operator_kind) {
        case MINIC_UNARY_PLUS:
            *value = operand;
            return true;
        case MINIC_UNARY_NEGATE:
            if (operand == INT64_MIN) {
                return false;
            }
            *value = -operand;
            return true;
        case MINIC_UNARY_LOGICAL_NOT:
            *value = operand == 0 ? 1 : 0;
            return true;
        case MINIC_UNARY_BITWISE_NOT:
            *value = ~operand;
            return true;
        default:
            return false;
        }
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY) {
        int64_t left;
        int64_t right;

        if (!builtin_constant_integer_value(program, expression->value.binary.left, &left) ||
            !builtin_constant_integer_value(program, expression->value.binary.right, &right)) {
            return false;
        }
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            if ((right > 0 && left > INT64_MAX - right) ||
                (right < 0 && left < INT64_MIN - right)) {
                return false;
            }
            *value = left + right;
            return true;
        case MINIC_BINARY_SUBTRACT:
            if ((right < 0 && left > INT64_MAX + right) ||
                (right > 0 && left < INT64_MIN + right)) {
                return false;
            }
            *value = left - right;
            return true;
        case MINIC_BINARY_MULTIPLY:
            if (left != 0 &&
                ((left == -1 && right == INT64_MIN) ||
                 (right == -1 && left == INT64_MIN) ||
                 (left > 0 && right > 0 && left > INT64_MAX / right) ||
                 (left > 0 && right < 0 && right < INT64_MIN / left) ||
                 (left < 0 && right > 0 && left < INT64_MIN / right) ||
                 (left < 0 && right < 0 && left < INT64_MAX / right))) {
                return false;
            }
            *value = left * right;
            return true;
        case MINIC_BINARY_DIVIDE:
            if (right == 0 || (left == INT64_MIN && right == -1)) {
                return false;
            }
            *value = left / right;
            return true;
        case MINIC_BINARY_REMAINDER:
            if (right == 0 || (left == INT64_MIN && right == -1)) {
                return false;
            }
            *value = left % right;
            return true;
        case MINIC_BINARY_SHIFT_LEFT:
            if (right < 0 || right >= 63 || left < 0 || left > (INT64_MAX >> right)) {
                return false;
            }
            *value = left << right;
            return true;
        case MINIC_BINARY_SHIFT_RIGHT:
            if (right < 0 || right >= 63) {
                return false;
            }
            *value = left >> right;
            return true;
        case MINIC_BINARY_BITWISE_AND:
            *value = left & right;
            return true;
        case MINIC_BINARY_BITWISE_XOR:
            *value = left ^ right;
            return true;
        case MINIC_BINARY_BITWISE_OR:
            *value = left | right;
            return true;
        case MINIC_BINARY_EQUAL:
            *value = left == right ? 1 : 0;
            return true;
        case MINIC_BINARY_NOT_EQUAL:
            *value = left != right ? 1 : 0;
            return true;
        case MINIC_BINARY_LESS:
            *value = left < right ? 1 : 0;
            return true;
        case MINIC_BINARY_LESS_EQUAL:
            *value = left <= right ? 1 : 0;
            return true;
        case MINIC_BINARY_GREATER:
            *value = left > right ? 1 : 0;
            return true;
        case MINIC_BINARY_GREATER_EQUAL:
            *value = left >= right ? 1 : 0;
            return true;
        case MINIC_BINARY_LOGICAL_AND:
            *value = left != 0 && right != 0 ? 1 : 0;
            return true;
        case MINIC_BINARY_LOGICAL_OR:
            *value = left != 0 || right != 0 ? 1 : 0;
            return true;
        default:
            return false;
        }
    }
    return false;
}

static bool parse_builtin_types_compatible_p(MinicParser *parser,
                                              MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicSourcePosition begin;
    MinicType left_type;
    MinicType right_type;

    if (!generic_token_text_equals(parser, "__builtin_types_compatible_p")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_LPAREN,
                             "expected '(' after __builtin_types_compatible_p") ||
        !minic_parser_parse_type_name(parser, &left_type) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_COMMA,
                             "expected ',' in __builtin_types_compatible_p") ||
        !minic_parser_parse_type_name(parser, &right_type)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span.begin = begin;
    expression.span.end = parser->current.span.end;
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = generic_types_compatible(left_type, right_type) ? 1 : 0;
    return minic_parser_expect(parser,
                               MINIC_TOKEN_RPAREN,
                               "expected ')' after __builtin_types_compatible_p") &&
           minic_parser_add_expression(parser, &expression, expression_id);
}

static bool parse_builtin_choose_expr(MinicParser *parser,
                                      MinicExpressionId *expression_id,
                                      bool decay_array) {
    MinicExpressionId condition_id;
    MinicExpressionId when_true_id;
    MinicExpressionId when_false_id;
    int64_t condition_value;

    if (!generic_token_text_equals(parser, "__builtin_choose_expr")) {
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_LPAREN,
                             "expected '(' after __builtin_choose_expr") ||
        !parse_expression_internal(parser, &condition_id, 0U, true) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_COMMA,
                             "expected first ',' in __builtin_choose_expr") ||
        !parse_expression_internal(parser, &when_true_id, 0U, decay_array) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_COMMA,
                             "expected second ',' in __builtin_choose_expr") ||
        !parse_expression_internal(parser, &when_false_id, 0U, decay_array) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_RPAREN,
                             "expected ')' after __builtin_choose_expr")) {
        return false;
    }
    if (!builtin_constant_integer_value(parser->program, condition_id, &condition_value)) {
        minic_parser_error(parser,
                           "__builtin_choose_expr condition must be an integer constant expression");
        return false;
    }
    *expression_id = condition_value != 0 ? when_true_id : when_false_id;
    return true;
}

'''
text = text.replace(marker, helpers + marker, 1)

entry = """    if (generic_token_text_equals(parser, "_Generic")) {
"""
replacement = """    if (generic_token_text_equals(parser, "__builtin_types_compatible_p")) {
        if (!parse_builtin_types_compatible_p(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_choose_expr")) {
        if (!parse_builtin_choose_expr(parser, &primary_id, decay_array) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "_Generic")) {
"""
if text.count(entry) != 1:
    raise SystemExit(f"GNU selection builtins: expected one _Generic entry, found {text.count(entry)}")
path.write_text(text.replace(entry, replacement, 1))

print("staged __builtin_types_compatible_p and __builtin_choose_expr with compile-time frontend selection")
