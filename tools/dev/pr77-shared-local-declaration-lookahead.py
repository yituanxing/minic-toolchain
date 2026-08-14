#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()
start = text.find("static bool token_starts_local_declaration(const MinicParser *parser) {")
end = text.find("\nstatic bool token_starts_expression", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate local declaration lookahead")
replacement = r'''static bool token_starts_local_declaration(const MinicParser *parser) {
    return parser != NULL && minic_parser_token_starts_type_name(parser, parser->current);
}
'''
text = text[:start] + replacement + text[end:]
path.write_text(text)
print("shared local declaration dispatch with parser_type type-name lookahead")
