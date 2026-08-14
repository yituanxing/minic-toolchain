#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_internal.h",
    """bool minic_parser_parse_integer_value(MinicParser *parser, int *value);
bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value);
""",
    """bool minic_parser_parse_integer_value(MinicParser *parser, int *value);
bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value);
bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value);
""",
)

replace_once(
    "src/frontend/parser_core.c",
    """bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    int64_t value;

    if (element_count == NULL || !parse_array_bound_additive(parser, &value)) {
""",
    """bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value) {
    return parser != NULL && value != NULL && parse_array_bound_additive(parser, value);
}

bool minic_parser_parse_fixed_array_bound(MinicParser *parser, size_t *element_count) {
    int64_t value;

    if (element_count == NULL || !minic_parser_parse_integer_constant_expression(parser, &value)) {
""",
)

replace_once(
    "src/frontend/parser_typedef.c",
    """static bool parse_enum_integer_value(MinicParser *parser, int *value) {
    bool negative;
    int parsed;

    negative = parser->current.kind == MINIC_TOKEN_MINUS;
    if (negative && !minic_parser_advance(parser)) {
        return false;
    }
    if (!minic_parser_parse_integer_value(parser, &parsed)) {
        return false;
    }
    *value = negative ? -parsed : parsed;
    return true;
}
""",
    """static bool parse_enum_integer_value(MinicParser *parser, int *value) {
    int64_t parsed;

    if (parser == NULL || value == NULL ||
        !minic_parser_parse_integer_constant_expression(parser, &parsed)) {
        return false;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        minic_parser_error(parser, \"enum constant expression is out of int range\");
        return false;
    }
    *value = (int)parsed;
    return true;
}
""",
)

print("staged enum initializers through the shared integer constant-expression evaluator")
