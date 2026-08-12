from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"anchor mismatch {path}: {count}")
    p.write_text(text.replace(old, new, 1))


old = '''    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_section_attribute(
            parser, section_name, sizeof(section_name), &section_name_length, &has_section) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &return_type) ||
        !minic_parser_parse_gnu_section_attribute(
            parser, section_name, sizeof(section_name), &section_name_length, &has_section) ||
        !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes)) {
        return false;
    }
'''
new = '''    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &return_type) ||
        !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes)) {
        return false;
    }
'''
replace_once("src/frontend/parser_function.c", old, new)

Path("tests/compiler/c0/deferred_declarator_attributes.c").write_text(r'''void __attribute__((__section__(".text.preptr"))) __attribute__((__noinline__))
*map_before_pointer(int value);

void *map_before_pointer(int value) {
    return value ? (void *)0 : (void *)0;
}

void *__attribute__((__noinline__)) map_after_pointer(int value);

void *map_after_pointer(int value) {
    return value ? (void *)0 : (void *)0;
}

extern char __attribute__((__section__(".data.preptr"))) *extern_slot_before_pointer;

int use_deferred_declarator_attributes(void) {
    return map_before_pointer(1) == (void *)0 && map_after_pointer(2) == (void *)0;
}
''')

Path("tests/compiler/c0/invalid_function_attribute_on_pointer_object.c").write_text(r'''extern int __attribute__((__noinline__)) *bad_object;
''')

Path("tests/compiler/c0/run-deferred-declarator-attributes.sh").write_text(r'''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-deferred-declarator-attributes

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/deferred_declarator_attributes.c" \
    -o "$work/deferred_declarator_attributes.s"
test -s "$work/deferred_declarator_attributes.s"
grep -F '.section .text.preptr' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'map_before_pointer:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'map_after_pointer:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'call map_before_pointer' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'call map_after_pointer' "$work/deferred_declarator_attributes.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_function_attribute_on_pointer_object.c" \
    -o "$work/invalid.s" >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/deferred_declarator_attributes: function-only attribute leaked onto object' >&2
    exit 1
fi
grep -F 'unsupported GNU object attribute' "$work/invalid.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/deferred_declarator_attributes pre-pointer=generic post-pointer=generic function-target=late object-target=late section=preserved noinline=parse-only'
''')

run_sh = Path("tests/compiler/c0/run.sh")
text = run_sh.read_text()
needle = 'MINIC="$minic" BUILD_DIR="$work/conditional-null-pointer-constant" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-conditional-null-pointer-constant.sh"\n'
if text.count(needle) != 1:
    raise SystemExit(f"run.sh conditional-null anchor mismatch: {text.count(needle)}")
text = text.replace(
    needle,
    needle + '\nMINIC="$minic" BUILD_DIR="$work/deferred-declarator-attributes" HOST_CC="$host_cc" sh "$root/tests/compiler/c0/run-deferred-declarator-attributes.sh"\n',
    1,
)
run_sh.write_text(text)
