from pathlib import Path

path = Path('tools/dev/pr133-materialize.py')
text = path.read_text()
old = '''bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType element_type,
                                                 MinicSourceSpan name_span,
'''
new = '''bool minic_parser_parse_static_global_after_head(MinicParser *parser,
                                                 MinicType object_type,
                                                 MinicSourceSpan name_span,
'''
if text.count(old) != 1:
    raise SystemExit(f'PR133 header-anchor repair mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1))
