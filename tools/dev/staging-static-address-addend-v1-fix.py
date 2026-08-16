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

old_diagnostic = (
    '"static pointer initializer requires a null or zero-addend symbol address "\n'
    '                       "constant"'
)
new_diagnostic = (
    '"static pointer initializer requires a null or static symbol address "\n'
    '                       "constant"'
)
if t.count(old_diagnostic) != 1:
    raise SystemExit(f'expected one split zero-addend diagnostic, got {t.count(old_diagnostic)}')
t = t.replace(old_diagnostic, new_diagnostic, 1)

pattern = re.compile(
    r'static bool function_designator_type\(.*?(?=static bool static_pointer_expression_has_explicit_cast)',
    re.S,
)
t, count = pattern.subn('', t, count=1)
if count != 1:
    raise SystemExit(f'expected one obsolete function-pointer helper block, got {count}')

p.write_text(t)

p = Path('tests/compiler/c0/run-static-object-address-relocation.sh')
t = p.read_text()
old = 'test "$count" -eq 2\n'
if t.count(old) != 1:
    raise SystemExit(f'expected one old internal-address count, got {t.count(old)}')
t = t.replace(old, 'test "$count" -eq 3\n', 1)
p.write_text(t)
