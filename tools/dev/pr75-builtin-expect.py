#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


path = Path("src/frontend/parser_expression.c")
text = path.read_text()
marker = "static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {\n"
helper = r'''static bool current_identifier_is(const MinicParser *parser, const char *name) {
    size_t length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(name) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

static bool parse_builtin_expect(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourcePosition begin;
    MinicSourcePosition end;
    MinicExpression conversion;
    MinicExpressionId value_id;
    const MinicExpression *value;
    int64_t expected_value;

    if (parser == NULL || expression_id == NULL ||
        !current_identifier_is(parser, "__builtin_expect")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_expect") ||
        !minic_parser_parse_expression(parser, &value_id, 0U)) {
        return false;
    }
    value = minic_c0_program_expression(parser->program, value_id);
    if (value == NULL || !minic_type_is_integer(value->type)) {
        minic_parser_error(parser, "__builtin_expect first argument must have integer type");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_COMMA || !minic_parser_advance(parser) ||
        !minic_parser_parse_integer_constant_expression(parser, &expected_value)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "__builtin_expect second argument must be an integer constant");
        }
        return false;
    }
    (void)expected_value;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_expect arguments");
        return false;
    }
    end = parser->current.span.end;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    /* GCC's documented type is long. The prediction hint itself has no runtime semantics,
     * so lower the builtin to the ordinary value conversion and leave optimization metadata
     * for a future IR/branch-probability pass. */
    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span.begin = begin;
    conversion.span.end = end;
    conversion.type = minic_type_long();
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = value_id;
    return minic_parser_add_expression(parser, &conversion, expression_id);
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"parse_primary marker: expected 1 match, found {text.count(marker)}")
text = text.replace(marker, helper + marker, 1)
path.write_text(text)

replace_once(
    "src/frontend/parser_expression.c",
    """    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
""",
    """    if (current_identifier_is(parser, "__builtin_expect")) {
        if (!parse_builtin_expect(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
""",
)

print("staged GCC __builtin_expect as value-preserving long conversion with constant hint")
