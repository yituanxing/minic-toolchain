from pathlib import Path

p = Path("src/frontend/parser_postfix.c")
text = p.read_text()
old = """        if (parser->current.kind == MINIC_TOKEN_RPAREN ||
            !minic_parser_parse_expression(parser, &argument_id, 0U)) {
            indirect_argument_count_error(parser);
            return false;
        }
"""
new = """        if (parser->current.kind == MINIC_TOKEN_RPAREN) {
            indirect_argument_count_error(parser);
            return false;
        }
        if (!minic_parser_parse_expression(parser, &argument_id, 0U)) {
            return false;
        }
"""
if text.count(old) != 1:
    raise SystemExit(f"anchor mismatch: {text.count(old)}")
p.write_text(text.replace(old, new, 1))
