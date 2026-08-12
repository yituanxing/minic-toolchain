#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    file_path = Path(path)
    text = file_path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one exact anchor, found {count}")
    file_path.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_statement.c",
    '''static bool parse_static_local_declaration(MinicParser *parser) {
    MinicType base_type;

    if (parser->current_function == MINIC_FUNCTION_INVALID ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }
''',
    '''static bool consume_static_local_interleaved_attribute(
    MinicParser *parser, const MinicParsedAttribute *attribute, void *opaque_context) {
    const MinicAttributeDescriptor *descriptor;

    (void)opaque_context;
    if (parser == NULL || attribute == NULL) {
        return false;
    }
    descriptor = attribute->descriptor;
    if (descriptor == NULL) {
        minic_parser_error(parser, "unsupported GNU attribute on static local object");
        return false;
    }
    if (!minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT)) {
        minic_parser_error(parser, "GNU attribute is not valid on a static local object");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_UNUSED &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {
        return true;
    }
    minic_parser_error(
        parser, "GNU static local object attribute semantics are not implemented at this placement");
    return false;
}

static bool parse_static_local_declaration(MinicParser *parser) {
    MinicType base_type;

    if (parser->current_function == MINIC_FUNCTION_INVALID ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_attribute_lists(
            parser, consume_static_local_interleaved_attribute, NULL)) {
        return false;
    }
''',
    "static local interleaved object attribute dispatch",
)

Path("tests/compiler/c0/gnu_static_local_interleaved_attribute.c").write_text(
    '''struct LockdepLike {
    int key;
    unsigned long state;
};

static int record_value(void)
{
    static struct LockdepLike __attribute__((__unused__)) map = {};
    return map.key + (int)map.state;
}

static int scalar_value(void)
{
    static int __attribute__((__unused__)) value = 7;
    return value;
}

int main(void)
{
    return record_value() == 0 && scalar_value() == 7 ? 0 : 1;
}
'''
)

Path("tests/compiler/c0/run-gnu-static-local-interleaved-attribute.sh").write_text(
    '''#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-static-local-interleaved-attribute

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_static_local_interleaved_attribute.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'record_value:' "$work/output.s" >/dev/null
grep -F 'scalar_value:' "$work/output.s" >/dev/null
grep -F '__minic_static_local_' "$work/output.s" >/dev/null

cat >"$work/layout-attribute.c" <<'EOF'
int bad(void)
{
    static int __attribute__((aligned(16))) value;
    return value;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/layout-attribute.c" -o "$work/layout-attribute.i"
if "$minic" -S "$work/layout-attribute.i" -o "$work/layout-attribute.s" \
    2>"$work/layout-attribute.stderr"; then
    printf '%s\n' 'layout-bearing static-local interleaved attribute unexpectedly ignored' >&2
    exit 1
fi
grep -F 'GNU static local object attribute semantics are not implemented at this placement' \
    "$work/layout-attribute.stderr" >/dev/null

printf '%s\n' \
  'PASS compiler/c0/gnu_static_local_interleaved_attribute placement=type-before-declarator unused=informational record-empty-init=zero scalar=preserved layout-bearing=fail-closed'
'''
)

run_path = Path("tests/compiler/c0/run.sh")
run_text = run_path.read_text()
needle = 'sh "$root/tests/compiler/c0/run-gnu-omitted-conditional.sh"\n'
if needle not in run_text:
    raise SystemExit("C0 runner insertion anchor missing")
insert = needle + '''\nMINIC="$minic" \\
HOST_CC="$host_cc" \\
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\
sh "$root/tests/compiler/c0/run-gnu-static-local-interleaved-attribute.sh"\n'''
run_path.write_text(run_text.replace(needle, insert, 1))
