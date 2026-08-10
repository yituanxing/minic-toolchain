#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_expression.c")
text = path.read_text()
marker = "static bool parse_primary(MinicParser *parser, MinicExpressionId *expression_id, bool decay_array) {\n"
if text.count(marker) != 1:
    raise SystemExit(f"builtin constant-p: expected one parse_primary marker, found {text.count(marker)}")

helper = r'''static bool parse_builtin_constant_p(MinicParser *parser,
                                     MinicExpressionId *expression_id) {
    MinicExpression result;
    MinicExpressionId operand_id;
    MinicSourcePosition begin;
    MinicSourcePosition end;
    int64_t constant_value;
    bool is_constant;

    if (parser == NULL || expression_id == NULL ||
        !generic_token_text_equals(parser, "__builtin_constant_p")) {
        return false;
    }
    begin = parser->current.span.begin;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser,
                             MINIC_TOKEN_LPAREN,
                             "expected '(' after __builtin_constant_p") ||
        !parse_expression_internal(parser, &operand_id, 0U, true)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        minic_parser_error(parser, "expected ')' after __builtin_constant_p operand");
        return false;
    }
    end = parser->current.span.end;
    is_constant = builtin_constant_integer_value(parser->program, operand_id, &constant_value);
    (void)constant_value;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    /* __builtin_constant_p is a compile-time query. The operand is parsed for C
     * semantics but its AST edge is intentionally not retained, so it is never
     * evaluated at runtime. This first generic implementation recognizes the
     * integer constant-expression subset already shared by choose_expr; unknown
     * expressions conservatively produce 0, as required by GCC's contract. */
    (void)memset(&result, 0, sizeof(result));
    result.kind = MINIC_EXPRESSION_INTEGER;
    result.span.begin = begin;
    result.span.end = end;
    result.type = minic_type_int();
    result.value_category = MINIC_VALUE_RVALUE;
    result.value.integer_value = is_constant ? 1 : 0;
    return minic_parser_add_expression(parser, &result, expression_id);
}

'''
text = text.replace(marker, helper + marker, 1)

entry = '''    if (generic_token_text_equals(parser, "__builtin_types_compatible_p")) {
'''
replacement = '''    if (generic_token_text_equals(parser, "__builtin_constant_p")) {
        if (!parse_builtin_constant_p(parser, &primary_id) ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
    if (generic_token_text_equals(parser, "__builtin_types_compatible_p")) {
'''
if text.count(entry) != 1:
    raise SystemExit(f"builtin constant-p: expected one builtin entry anchor, found {text.count(entry)}")
path.write_text(text.replace(entry, replacement, 1))
print("staged GNU __builtin_constant_p as a conservative compile-time integer constness query")
