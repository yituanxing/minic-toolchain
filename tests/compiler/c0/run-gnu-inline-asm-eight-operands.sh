#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
rv_cc=${RV_CC:-riscv64-linux-gnu-gcc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-eight-operands
rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_inline_asm_eight_operands.c" -o "$work/probe.i"
"$minic" -S "$work/probe.i" -o "$work/probe.s"
test -s "$work/probe.s"
grep -F 'add a0, a0, a2' "$work/probe.s" >/dev/null
grep -F '# sbi a1 a3 a4 a5 a6 a7' "$work/probe.s" >/dev/null
"$rv_cc" -c "$work/probe.s" -o "$work/probe.o"
printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_eight_operands total=8 outputs=2 inputs=6 bindings=a0-a7 assembly=1'
