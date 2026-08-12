#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_record.c"
text = path.read_text()
old = '''static bool token_text_equals(const MinicParser *parser, MinicToken token, const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || token.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(token.span);
    return strlen(text) == length &&
           memcmp(parser->source + token.span.begin.offset, text, length) == 0;
}

'''
if text.count(old) != 1:
    raise SystemExit(f"expected one obsolete token_text_equals helper, found {text.count(old)}")
path.write_text(text.replace(old, '', 1))
