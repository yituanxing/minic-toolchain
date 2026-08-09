#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()

anchor = "static bool parse_for(MinicParser *parser) {\n"
if text.count(anchor) != 1:
    raise SystemExit("parse_for anchor mismatch")
text = text.replace(
    anchor,
    "static bool token_starts_local_declaration(const MinicParser *parser);\n\n" + anchor,
    1,
)

old = '''    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!parse_expression_or_assignment_statement(parser, false)) {
        return false;
    }
'''
new = '''    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('")) {
        return false;
    }
    if (!minic_parser_begin_scope(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (token_starts_local_declaration(parser)) {
        if (!parse_declaration(parser)) {
            return false;
        }
    } else if (!parse_expression_or_assignment_statement(parser, false)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit("for initializer anchor mismatch")
text = text.replace(old, new, 1)

old = '''    statement.span.begin = for_span.begin;
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}
'''
new = '''    statement.span.begin = for_span.begin;
    statement.span.end = parser->current.span.begin;
    success = minic_parser_add_statement(parser, &statement);
    minic_parser_end_scope(parser);
    return success;
}
'''
if text.count(old) != 1:
    raise SystemExit("for scope cleanup anchor mismatch")
text = text.replace(old, new, 1)

path.write_text(text)
print("staged C99 for-init declarations with loop scope")
