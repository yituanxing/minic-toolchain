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
void unsupported(int values[static 4]);
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/static-bound.c" -o "$work/static-bound.i"
if "$minic" -S "$work/static-bound.i" -o "$work/static-bound.s" 2>"$work/static-bound.stderr"; then
    printf '%s
' 'parameter [static N] unexpectedly accepted by bounded v0' >&2
    exit 1
fi
test -s "$work/static-bound.stderr"

printf '%s
'   'PASS compiler/c0/array_parameter_adjustment named=1 unnamed=1 fixed-bound=discarded function-type=pointer redeclaration=array-pointer-compatible function-pointer-typedef=1 sizeof=pointer static-bound=fail-closed'
