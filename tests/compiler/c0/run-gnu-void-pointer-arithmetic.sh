#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-void-pointer-arithmetic
assembly="$work/gnu_void_pointer_arithmetic.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_void_pointer_arithmetic.c" \
    -o "$work/gnu_void_pointer_arithmetic.i"
"$minic" -S "$work/gnu_void_pointer_arithmetic.i" -o "$assembly"

test -s "$assembly"
grep -F 'gnu_void_pointer_add:' "$assembly" >/dev/null
grep -F 'gnu_void_pointer_subtract:' "$assembly" >/dev/null
grep -F 'gnu_void_pointer_difference:' "$assembly" >/dev/null
grep -F 'linux_void_pointer_difference:' "$assembly" >/dev/null
grep -F 'gnu_function_pointer_difference:' "$assembly" >/dev/null

cat >"$work/mismatched-pointer-difference.c" <<'EOF'
long bad_difference(int *left, long *right)
{
    return left - right;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/mismatched-pointer-difference.c" \
    -o "$work/mismatched-pointer-difference.i"
if "$minic" -S "$work/mismatched-pointer-difference.i" \
    -o "$work/mismatched-pointer-difference.s" \
    2>"$work/mismatched-pointer-difference.stderr"; then
    printf '%s\n' 'mismatched pointer difference unexpectedly accepted' >&2
    exit 1
fi
grep -F 'unsupported pointer arithmetic operands' "$work/mismatched-pointer-difference.stderr" >/dev/null

cat >"$work/incomplete-pointer-difference.c" <<'EOF'
struct Incomplete;
long bad_incomplete_difference(struct Incomplete *left, struct Incomplete *right)
{
    return left - right;
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/incomplete-pointer-difference.c" \
    -o "$work/incomplete-pointer-difference.i"
if "$minic" -S "$work/incomplete-pointer-difference.i" \
    -o "$work/incomplete-pointer-difference.s" \
    2>"$work/incomplete-pointer-difference.stderr"; then
    printf '%s\n' 'incomplete object pointer difference unexpectedly accepted' >&2
    exit 1
fi
grep -F 'unsupported pointer arithmetic operands' "$work/incomplete-pointer-difference.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_void_pointer_arithmetic pointee=void/function stride=1 binary=+,- difference=void+function mismatched=reject incomplete-record=unchanged'
