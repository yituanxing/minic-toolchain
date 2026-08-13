from pathlib import Path

root = Path('.')

def replace_once(path, old, new, label):
    p = root / path
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label} mismatch: {count}')
    p.write_text(text.replace(old, new, 1))

replace_once(
    'src/frontend/parser_core.c',
    '''        minic_parser_error(parser,\n                           "integer initializer requires a convertible constant expression");\n''',
    '''        minic_parser_error(parser,\n                           "integer initializer requires an integer constant expression");\n''',
    'integer nonconstant diagnostic')
replace_once(
    'src/frontend/parser_core.c',
    '''        minic_parser_error(parser, "integer initializer exceeds legacy int payload range");\n''',
    '''        minic_parser_error(parser, "integer initializer exceeds current global payload range");\n''',
    'legacy int payload diagnostic')

p = root / 'src/frontend/parser_global.c'
text = p.read_text()
old = '''    minic_parser_error(parser,\n                       "static pointer initializer requires null, symbolic address, or explicit "\n                       "integer-to-pointer constant cast");\n'''
new = '''    minic_parser_error(parser,\n                       "static pointer initializer requires a null or zero-addend object address "\n                       "constant");\n'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'shared pointer initializer diagnostic mismatch: {count}')
text = text.replace(old, new, 1)
old = '''                    minic_parser_error(\n                        parser,\n                        "static pointer initializer requires null, symbolic address, "\n                        "or explicit integer-to-pointer constant cast");\n'''
new = '''                    minic_parser_error(\n                        parser,\n                        "static pointer initializer requires a null or zero-addend object address "\n                        "constant");\n'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'top-level pointer initializer diagnostic mismatch: {count}')
p.write_text(text.replace(old, new, 1))
