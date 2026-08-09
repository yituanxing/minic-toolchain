#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


path = Path("src/frontend/parser_core.c")
text = path.read_text()

text = replace_once(
    text,
    '''static bool parse_array_bound_additive(MinicParser *parser, int64_t *value);\nstatic bool parse_array_bound_unary(MinicParser *parser, int64_t *value);\n''',
    '''static bool parse_array_bound_additive(MinicParser *parser, int64_t *value);\nstatic bool parse_array_bound_bitwise_or(MinicParser *parser, int64_t *value);\nstatic bool parse_array_bound_unary(MinicParser *parser, int64_t *value);\n''',
    "constant-expression forward declarations",
)

text = replace_once(
    text,
    '''        if (!minic_parser_advance(parser) || !parse_array_bound_additive(parser, value) ||\n            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in array bound")) {\n''',
    '''        if (!minic_parser_advance(parser) || !parse_array_bound_bitwise_or(parser, value) ||\n            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in integer constant expression")) {\n''',
    "parenthesized constant expression",
)

old_unary = '''    operator_kind = parser->current.kind;\n    if (operator_kind != MINIC_TOKEN_PLUS && operator_kind != MINIC_TOKEN_MINUS) {\n        return parse_array_bound_primary(parser, value);\n    }\n    if (!minic_parser_advance(parser) || !parse_array_bound_unary(parser, &operand)) {\n        return false;\n    }\n    if (operator_kind == MINIC_TOKEN_MINUS) {\n        if (operand == INT64_MIN) {\n            minic_parser_error(parser, "array bound constant expression overflow");\n            return false;\n        }\n        operand = -operand;\n    }\n    *value = operand;\n    return true;\n'''
new_unary = '''    operator_kind = parser->current.kind;\n    if (operator_kind != MINIC_TOKEN_PLUS && operator_kind != MINIC_TOKEN_MINUS &&\n        operator_kind != MINIC_TOKEN_TILDE && operator_kind != MINIC_TOKEN_BANG) {\n        return parse_array_bound_primary(parser, value);\n    }\n    if (!minic_parser_advance(parser) || !parse_array_bound_unary(parser, &operand)) {\n        return false;\n    }\n    if (operator_kind == MINIC_TOKEN_MINUS) {\n        if (operand == INT64_MIN) {\n            minic_parser_error(parser, "integer constant expression overflow");\n            return false;\n        }\n        operand = -operand;\n    } else if (operator_kind == MINIC_TOKEN_TILDE) {\n        operand = (int64_t)(~(uint64_t)operand);\n    } else if (operator_kind == MINIC_TOKEN_BANG) {\n        operand = operand == 0 ? 1 : 0;\n    }\n    *value = operand;\n    return true;\n'''
text = replace_once(text, old_unary, new_unary, "constant-expression unary operators")

marker = '''bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value) {\n'''
helper = r'''static bool parse_array_bound_shift(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_additive(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_LESS_LESS ||
           parser->current.kind == MINIC_TOKEN_GREATER_GREATER) {
        MinicTokenKind operator_kind;
        int64_t right;

        operator_kind = parser->current.kind;
        if (!minic_parser_advance(parser) || !parse_array_bound_additive(parser, &right)) {
            return false;
        }
        if (right < 0 || right >= 64) {
            minic_parser_error(parser, "shift count is out of range in integer constant expression");
            return false;
        }
        if (operator_kind == MINIC_TOKEN_LESS_LESS) {
            if (left < 0 || (right != 0 && left > (INT64_MAX >> (unsigned int)right))) {
                minic_parser_error(parser, "left shift overflows integer constant expression");
                return false;
            }
            left <<= (unsigned int)right;
        } else {
            left >>= (unsigned int)right;
        }
    }
    *value = left;
    return true;
}

static bool parse_array_bound_bitwise_and(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_shift(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_AMPERSAND) {
        int64_t right;

        if (!minic_parser_advance(parser) || !parse_array_bound_shift(parser, &right)) {
            return false;
        }
        left = (int64_t)((uint64_t)left & (uint64_t)right);
    }
    *value = left;
    return true;
}

static bool parse_array_bound_bitwise_xor(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_bitwise_and(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_CARET) {
        int64_t right;

        if (!minic_parser_advance(parser) || !parse_array_bound_bitwise_and(parser, &right)) {
            return false;
        }
        left = (int64_t)((uint64_t)left ^ (uint64_t)right);
    }
    *value = left;
    return true;
}

static bool parse_array_bound_bitwise_or(MinicParser *parser, int64_t *value) {
    int64_t left;

    if (!parse_array_bound_bitwise_xor(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_PIPE) {
        int64_t right;

        if (!minic_parser_advance(parser) || !parse_array_bound_bitwise_xor(parser, &right)) {
            return false;
        }
        left = (int64_t)((uint64_t)left | (uint64_t)right);
    }
    *value = left;
    return true;
}

'''
text = replace_once(text, marker, helper + marker, "constant-expression bitwise hierarchy")
text = replace_once(
    text,
    '''    return parser != NULL && value != NULL && parse_array_bound_additive(parser, value);\n''',
    '''    return parser != NULL && value != NULL && parse_array_bound_bitwise_or(parser, value);\n''',
    "shared integer constant-expression entrypoint",
)

path.write_text(text)
print("staged shift and bitwise operators in shared integer constant expressions")
