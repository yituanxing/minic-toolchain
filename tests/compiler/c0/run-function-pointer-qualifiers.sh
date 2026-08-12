#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-pointer-qualifiers

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/function_pointer_qualifiers.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'call_filter:' "$work/output.s" >/dev/null
grep -F 'call_hook:' "$work/output.s" >/dev/null
grep -F 'call_typedef:' "$work/output.s" >/dev/null
grep -F 'jalr' "$work/output.s" >/dev/null

cat >"$work/const-field-assign.c" <<'EOF'
typedef int callback_t(int);
struct Ops { int (* const filter)(int); };
void bad(struct Ops *ops, callback_t *callback)
{
    ops->filter = callback;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/const-field-assign.c" -o "$work/const-field-assign.i"
if "$minic" -S "$work/const-field-assign.i" -o "$work/const-field-assign.s" 2>"$work/const-field-assign.stderr"; then
    printf '%s\n' 'const-qualified function pointer field unexpectedly modifiable' >&2
    exit 1
fi
test -s "$work/const-field-assign.stderr"

printf '%s\n' 'PASS compiler/c0/function_pointer_qualifiers record-field=const+volatile typedef=const indirect-call=1 const-field=nonmodifiable shared-pointer-qualifier-parser=1'
