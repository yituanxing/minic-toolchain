from pathlib import Path

path = Path('tools/dev/pr134-materialize.py')
text = path.read_text()
old = '''anchor = ''' + "'''" + '''bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value);\nbool minic_parser_parse_integer_constant_expression_value(MinicParser *parser, int64_t *value);\n''' + "'''"
new = '''anchor = ''' + "'''" + '''bool minic_parser_parse_integer_constant_expression(MinicParser *parser, int64_t *value);\n''' + "'''"
if text.count(old) != 1:
    raise SystemExit(f'PR134 header anchor repair mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1))
