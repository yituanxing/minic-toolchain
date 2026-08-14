#!/usr/bin/env python3
from pathlib import Path


path = Path("src/frontend/parser_typedef.c")
text = path.read_text()

marker = "bool minic_parser_parse_typedef(MinicParser *parser) {\n"
helper = r'''static bool typedef_token_text_equals(const MinicParser *parser, const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(text) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, text, length) == 0;
}

static bool parse_redundant_typedef_alignment(MinicParser *parser, MinicType aliased_type) {
    int64_t alignment;

    if (!typedef_token_text_equals(parser, "__attribute__") &&
        !typedef_token_text_equals(parser, "__attribute")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after typedef __attribute__") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' in typedef __attribute__")) {
        return false;
    }
    if (!typedef_token_text_equals(parser, "aligned") &&
        !typedef_token_text_equals(parser, "__aligned__")) {
        minic_parser_error(parser, "unsupported GNU typedef attribute");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after typedef aligned") ||
        !minic_parser_parse_integer_value64(parser, &alignment) || alignment <= 0 ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after typedef alignment") ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in typedef attribute") ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected second ')' in typedef attribute")) {
        if (parser != NULL && parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "typedef alignment must be a positive integer");
        }
        return false;
    }

    /* This discovery bridge is deliberately narrower than GCC's full attributed-type
       semantics.  Linux first reaches aligned(16) on GNU __int128, whose natural RV64
       alignment is already 16.  Accept that semantics-preserving spelling, but reject
       any alignment that would alter the type.  Permanent support belongs in the
       Declarator/AttributeSet + Target DataLayout architecture rather than silently
       discarding an ABI-affecting attribute here. */
    if (!minic_type_is_int128_integer(aliased_type) || alignment != 16) {
        minic_parser_error(
            parser,
            "non-redundant GNU typedef alignment requires attributed-type support");
        return false;
    }
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"typedef parser marker: expected one match, found {text.count(marker)}")
text = text.replace(marker, helper + marker, 1)

old = '''    if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(parser, "expected ';' after typedef");
        return false;
    }
'''
new = '''    if (!parse_redundant_typedef_alignment(parser, aliased_type)) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_SEMICOLON) {
        minic_parser_error(parser, "expected ';' after typedef");
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"typedef semicolon anchor: expected one match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged semantics-preserving GNU typedef aligned(16) for natural RV64 __int128")
