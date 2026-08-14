#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-named-operands
assembly="$work/gnu_inline_asm_named_operands.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_inline_asm_named_operands.c" \
    -o "$work/gnu_inline_asm_named_operands.i"
"$minic" -S "$work/gnu_inline_asm_named_operands.i" -o "$assembly"

test -s "$assembly"
grep -E 'lr\.w[[:space:]]+t0,[[:space:]]*\(t3\)' "$assembly" >/dev/null
grep -E 'beq[[:space:]]+t0,[[:space:]]*t5,[[:space:]]*1f' "$assembly" >/dev/null
grep -E 'add[[:space:]]+t1,[[:space:]]*t0,[[:space:]]*t4' "$assembly" >/dev/null
grep -E 'sc\.w\.rl[[:space:]]+t1,[[:space:]]*t1,[[:space:]]*\(t3\)' "$assembly" >/dev/null
if grep -F '%[' "$assembly" >/dev/null; then
    printf '%s\n' 'unexpected named GNU asm placeholder in emitted assembly' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_named_operands names=p,rc,c,a,u early-clobber==&r address=+A input=r target=RV64'
