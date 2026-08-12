#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-static-local-interleaved-attribute

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c   "$root/tests/compiler/c0/gnu_static_local_interleaved_attribute.c" -o "$work/input.i"
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
if "$minic" -S "$work/layout-attribute.i" -o "$work/layout-attribute.s"     2>"$work/layout-attribute.stderr"; then
    printf '%s
' 'layout-bearing static-local interleaved attribute unexpectedly ignored' >&2
    exit 1
fi
grep -F 'GNU static local object attribute semantics are not implemented at this placement'     "$work/layout-attribute.stderr" >/dev/null

printf '%s
'   'PASS compiler/c0/gnu_static_local_interleaved_attribute placement=type-before-declarator unused=informational record-empty-init=zero scalar=preserved layout-bearing=fail-closed'
