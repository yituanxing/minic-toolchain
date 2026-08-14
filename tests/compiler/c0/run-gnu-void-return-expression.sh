#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-void-return-expression

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c   "$root/tests/compiler/c0/gnu_void_return_expression.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'return_void_call:' "$work/output.s" >/dev/null
grep -F 'return_void_cast:' "$work/output.s" >/dev/null
return_call=$(grep -n -m1 -F '  call mark_return_expression' "$work/output.s" | cut -d: -f1)
cleanup_call=$(grep -n -m1 -F '  call mark_cleanup' "$work/output.s" | cut -d: -f1)
test -n "$return_call"
test -n "$cleanup_call"
test "$return_call" -lt "$cleanup_call"

cat >"$work/nonvoid-return.c" <<'EOF'
void bad(void)
{
    return 1;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/nonvoid-return.c" -o "$work/nonvoid-return.i"
if "$minic" -S "$work/nonvoid-return.i" -o "$work/nonvoid-return.s"     2>"$work/nonvoid-return.stderr"; then
    printf '%s
' 'non-void expression in void return unexpectedly accepted' >&2
    exit 1
fi
grep -F 'void function cannot return a value'     "$work/nonvoid-return.stderr" >/dev/null

cat >"$work/void-in-nonvoid.c" <<'EOF'
static void sink(void) { }
int bad(void)
{
    return sink();
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/void-in-nonvoid.c" -o "$work/void-in-nonvoid.i"
if "$minic" -S "$work/void-in-nonvoid.i" -o "$work/void-in-nonvoid.s"     2>"$work/void-in-nonvoid.stderr"; then
    printf '%s
' 'void expression in non-void return unexpectedly accepted' >&2
    exit 1
fi
grep -F 'return expression does not match function return type'     "$work/void-in-nonvoid.stderr" >/dev/null

printf '%s
'   'PASS compiler/c0/gnu_void_return_expression void-call=1 void-cast=1 side-effect-before-cleanup=1 nonvoid-expression=reject nonvoid-function=unchanged'
