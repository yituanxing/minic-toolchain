#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-array-parameter-adjustment

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c   "$root/tests/compiler/c0/array_parameter_adjustment.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'generate_random_uuid:' "$work/output.s" >/dev/null
grep -F 'unnamed_array_parameter:' "$work/output.s" >/dev/null
grep -F 'adjusted_size:' "$work/output.s" >/dev/null

cat >"$work/static-bound.c" <<'EOF'
void bounded(int values[static 4]);
void bounded(int *values)
{
    values[0] = 7;
}
int call_bounded(void)
{
    int values[4] = { 0 };
    bounded(values);
    return values[0];
}
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/static-bound.c" -o "$work/static-bound.i"
"$minic" -S "$work/static-bound.i" -o "$work/static-bound.s"
test -s "$work/static-bound.s"
grep -F 'bounded:' "$work/static-bound.s" >/dev/null
grep -F 'call_bounded:' "$work/static-bound.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/array_parameter_adjustment named=1 unnamed=1 fixed-bound=discarded function-type=pointer redeclaration=array-pointer-compatible function-pointer-typedef=1 sizeof=pointer static-bound=adjusted-pointer'
