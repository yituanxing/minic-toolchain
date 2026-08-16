#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/record-conditional-materialization

mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/record_conditional_materialization.c" \
    -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"

grep -F '.Lminic_record_cond_false_' "$work/output.s" >/dev/null
grep -F '.Lminic_record_cond_end_' "$work/output.s" >/dev/null
grep -F '  call pgprot_from_va' "$work/output.s" >/dev/null
grep -F '  call consume' "$work/output.s" >/dev/null

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi

printf '%s\n' 'PASS compiler/c0/record_conditional_materialization record-rvalue=conditional producers=address,call sink=assignment,call-argument'
