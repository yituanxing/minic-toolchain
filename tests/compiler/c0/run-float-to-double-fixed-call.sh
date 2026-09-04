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

static float echo_float(float value)
{
    return value;
}

double widen_for_call(float value)
{
    return consume_double(value);
}

float direct_float_call(float value)
{
    return echo_float(value);
}

float indirect_float_call(float (*function)(float), float value)
{
    return function(value);
}
SRC

"$minic" -S "$work/input.c" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'widen_for_call:' "$work/output.s" >/dev/null
grep -F 'direct_float_call:' "$work/output.s" >/dev/null
grep -F 'indirect_float_call:' "$work/output.s" >/dev/null
grep -F '  fcvt.d.s ' "$work/output.s" >/dev/null
grep -F '  call consume_double' "$work/output.s" >/dev/null
grep -F '  fmv.w.x ' "$work/output.s" >/dev/null
grep -F '  fmv.x.w ' "$work/output.s" >/dev/null

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi

printf '%s\n' 'PASS compiler/c0/float-call-abi fixed-conversion=float->double parameter=float direct=float indirect=float return=float core=1'
