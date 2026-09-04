#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/float-to-double-fixed-call
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.c" <<'SRC'
static double consume_double(double value)
{
    return value;
}

double widen_for_call(float value)
{
    return consume_double(value);
}
SRC

"$minic" -S "$work/input.c" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'widen_for_call:' "$work/output.s" >/dev/null
grep -F '  fcvt.d.s ' "$work/output.s" >/dev/null
grep -F '  call consume_double' "$work/output.s" >/dev/null

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi

printf '%s\n' 'PASS compiler/c0/float-to-double-fixed-call conversion=float->double direct-call=1 core=1'
