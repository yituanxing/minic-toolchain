#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_declarator.c",
    '''           declarator_identifier_is(parser, "restrict") ||
           declarator_identifier_is(parser, "__restrict")) {
''',
    '''           declarator_identifier_is(parser, "restrict") ||
           declarator_identifier_is(parser, "__restrict") ||
           declarator_identifier_is(parser, "__restrict__")) {
''',
    "GNU __restrict__ pointer-qualifier alias",
)

Path("tests/compiler/c0/restrict_qualifiers.c").write_text(
    r'''extern void *copy_gnu(void *__restrict destination, const void *__restrict source, unsigned long count);
extern void *copy_gnu_double(void *__restrict__ destination,
                             const void *__restrict__ source,
                             unsigned long count);
extern void *copy_c(void *restrict destination, const void *restrict source, unsigned long count);

void *call_restrict_forms(void *destination, const void *source) {
    copy_gnu(destination, source, 4);
    copy_gnu_double(destination, source, 4);
    return copy_c(destination, source, 4);
}
'''
)

runner = Path("tests/compiler/c0/run-restrict-qualifiers.sh")
text = runner.read_text()
old = '''grep -F '  call copy_gnu' "$work/restrict_qualifiers.s" >/dev/null
grep -F '  call copy_c' "$work/restrict_qualifiers.s" >/dev/null
printf '%s\\n' 'PASS compiler/c0/restrict_qualifiers C=restrict GNU=__restrict pointer-only=1 abi=unchanged'
'''
new = '''grep -F '  call copy_gnu' "$work/restrict_qualifiers.s" >/dev/null
grep -F '  call copy_gnu_double' "$work/restrict_qualifiers.s" >/dev/null
grep -F '  call copy_c' "$work/restrict_qualifiers.s" >/dev/null
printf '%s\\n' 'PASS compiler/c0/restrict_qualifiers C=restrict GNU=__restrict/__restrict__ pointer-only=1 abi=unchanged'
'''
if text.count(old) != 1:
    raise SystemExit(f"restrict runner anchor: expected 1 match, found {text.count(old)}")
runner.write_text(text.replace(old, new, 1))

print("staged GNU __restrict__ alias through existing pointer qualifier owner")
