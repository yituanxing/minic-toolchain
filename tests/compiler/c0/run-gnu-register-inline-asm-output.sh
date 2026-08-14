#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-register-inline-asm-output
assembly="$work/gnu_register_inline_asm_output.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_register_inline_asm_output.c" \
    -o "$work/gnu_register_inline_asm_output.i"
"$minic" -S "$work/gnu_register_inline_asm_output.i" -o "$assembly"

test -s "$assembly"
grep -F 'read_cycle_like:' "$assembly" >/dev/null
grep -F 'csrr t0, 0xc01' "$assembly" >/dev/null
grep -F 'sd t0,' "$assembly" >/dev/null
if grep -F '%0' "$assembly" >/dev/null; then
    printf '%s\n' 'unsubstituted GNU asm operand placeholder in emitted assembly' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/gnu_register_inline_asm_output storage=register output==r placeholder=%0 target=t0 writeback=local memory-clobber=1'
