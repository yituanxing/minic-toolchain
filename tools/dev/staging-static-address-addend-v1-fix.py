from pathlib import Path
import re

p = Path('src/frontend/parser_global.c')
t = p.read_text()

t = t.replace(
    'initializer.relocation_target.member_depth)',
    'initializer.relocation_target.member_depth,\n                                     initializer.relocation_target.byte_addend)')
t = t.replace(
    'initializer->relocation_target.member_depth)',
    'initializer->relocation_target.member_depth,\n                             initializer->relocation_target.byte_addend)')

pattern = re.compile(
    r'static bool function_designator_type\(.*?(?=static bool static_pointer_expression_has_explicit_cast)',
    re.S,
)
t, count = pattern.subn('', t, count=1)
if count != 1:
    raise SystemExit(f'expected one obsolete function-pointer helper block, got {count}')

p.write_text(t)
