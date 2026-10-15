#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/arithmetic-fixed-call-dead-long-double
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.c" <<'SRC'
int classify_f(float value);
int classify_d(double value);
int classify_ld(long double value);

int probe(float value)
{
    return sizeof(float) == 4
        ? classify_f(value)
        : sizeof(float) == 8
              ? classify_d(value)
              : classify_ld(value);
}
SRC

"$minic" -S "$work/input.c" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'probe:' "$work/output.s" >/dev/null
grep -F '  call classify_f' "$work/output.s" >/dev/null
if grep -F '  call classify_d' "$work/output.s" >/dev/null ||
   grep -F '  call classify_ld' "$work/output.s" >/dev/null; then
    echo 'dead arithmetic call branch unexpectedly reached Core codegen' >&2
    exit 1
fi

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi

printf '%s\n' 'PASS compiler/c0/arithmetic-fixed-call dead-long-double=pruned float-to-long-double=frontend-only selected=float'
