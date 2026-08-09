#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_core.c")
text = path.read_text()
old = r'''bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    MinicSourceSpan span;
    size_t offset;
    size_t value;

    if (element_count == NULL || parser->current.kind != MINIC_TOKEN_INTEGER_CONSTANT) {
        minic_parser_error(parser, "expected integer array bound");
        return false;
    }

    span = parser->current.span;
    value = 0U;
    for (offset = span.begin.offset; offset < span.end.offset; ++offset) {
        size_t digit;

        digit = (size_t)(unsigned int)(parser->source[offset] - '0');
        if (value > (SIZE_MAX - digit) / 10U) {
            minic_parser_error(parser, "array bound exceeds target object range");
            return false;
        }
        value = value * 10U + digit;
    }
    if (value == 0U) {
        minic_parser_error(parser, "array bound must be greater than zero");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    *element_count = value;
    return true;
}
'''
new = r'''static bool parse_array_bound_additive(MinicParser *parser, int64_t *value);

static bool parse_array_bound_primary(MinicParser *parser, int64_t *value) {
    if (parser == NULL || value == NULL) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return minic_parser_parse_integer_value64(parser, value);
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) || !parse_array_bound_additive(parser, value) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in array bound")) {
            return false;
        }
        return true;
    }
    minic_parser_error(parser, "expected integer constant expression in array bound");
    return false;
}

static bool parse_array_bound_unary(MinicParser *parser, int64_t *value) {
    MinicTokenKind operator_kind;
    int64_t operand;

    if (parser == NULL || value == NULL) {
        return false;
    }
    operator_kind = parser->current.kind;
    if (operator_kind != MINIC_TOKEN_PLUS && operator_kind != MINIC_TOKEN_MINUS) {
        return parse_array_bound_primary(parser, value);
    }
    if (!minic_parser_advance(parser) || !parse_array_bound_unary(parser, &operand)) {
        return false;
    }
    if (operator_kind == MINIC_TOKEN_MINUS) {
        if (operand == INT64_MIN) {
            minic_parser_error(parser, "array bound constant expression overflow");
            return false;
        }
        operand = -operand;
    }
    *value = operand;
    return true;
}

static bool parse_array_bound_multiplicative(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_unary(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR ||
           parser->current.kind == MINIC_TOKEN_SLASH ||
           parser->current.kind == MINIC_TOKEN_PERCENT) {
        MinicTokenKind operator_kind;
        int64_t right;

        operator_kind = parser->current.kind;
        if (!minic_parser_advance(parser) || !parse_array_bound_unary(parser, &right)) {
            return false;
        }
        if (operator_kind == MINIC_TOKEN_STAR) {
            if (left != 0 &&
                ((left == -1 && right == INT64_MIN) ||
                 (right == -1 && left == INT64_MIN) ||
                 (left > 0 && right > 0 && left > INT64_MAX / right) ||
                 (left > 0 && right < 0 && right < INT64_MIN / left) ||
                 (left < 0 && right > 0 && left < INT64_MIN / right) ||
                 (left < 0 && right < 0 && left < INT64_MAX / right))) {
                minic_parser_error(parser, "array bound constant expression overflow");
                return false;
            }
            left *= right;
        } else {
            if (right == 0) {
                minic_parser_error(parser, "division by zero in array bound constant expression");
                return false;
            }
            if (left == INT64_MIN && right == -1) {
                minic_parser_error(parser, "array bound constant expression overflow");
                return false;
            }
            left = operator_kind == MINIC_TOKEN_SLASH ? left / right : left % right;
        }
    }
    *value = left;
    return true;
}

static bool parse_array_bound_additive(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_multiplicative(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_PLUS || parser->current.kind == MINIC_TOKEN_MINUS) {
        MinicTokenKind operator_kind;
        int64_t right;

        operator_kind = parser->current.kind;
        if (!minic_parser_advance(parser) || !parse_array_bound_multiplicative(parser, &right)) {
            return false;
        }
        if (operator_kind == MINIC_TOKEN_PLUS) {
            if ((right > 0 && left > INT64_MAX - right) ||
                (right < 0 && left < INT64_MIN - right)) {
                minic_parser_error(parser, "array bound constant expression overflow");
                return false;
            }
            left += right;
        } else {
            if ((right < 0 && left > INT64_MAX + right) ||
                (right > 0 && left < INT64_MIN + right)) {
                minic_parser_error(parser, "array bound constant expression overflow");
                return false;
            }
            left -= right;
        }
    }
    *value = left;
    return true;
}

bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    int64_t value;

    if (element_count == NULL || !parse_array_bound_additive(parser, &value)) {
        return false;
    }
    if (value <= 0) {
        minic_parser_error(parser, "array bound must be greater than zero");
        return false;
    }
    if ((uint64_t)value > (uint64_t)SIZE_MAX) {
        minic_parser_error(parser, "array bound exceeds target object range");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    *element_count = (size_t)value;
    return true;
}
'''
if text.count(old) != 1:
    raise SystemExit("unexpected fixed array bound parser")
path.write_text(text.replace(old, new, 1))
print("staged arithmetic integer constant expressions for fixed array bounds")
