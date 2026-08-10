#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-goto
assembly="$work/gnu_inline_asm_goto.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_inline_asm_goto.c" \
    -o "$work/gnu_inline_asm_goto.i"
"$minic" -S "$work/gnu_inline_asm_goto.i" -o "$assembly"

test -s "$assembly"
grep -E '^  j \.Luser_[0-9]+$' "$assembly" >/dev/null
grep -E '^\.Luser_[0-9]+:$' "$assembly" >/dev/null
grep -F '.word 33' "$assembly" >/dev/null
grep -E '\.long \.Luser_[0-9]+ - \.' "$assembly" >/dev/null
grep -E '__minic_deferred_asm_immediate_[0-9]+_[0-9]+' "$assembly" >/dev/null
grep -F 'MINIC_DEFERRED_ASM_IMMEDIATE requires inline specialization' "$assembly" >/dev/null
if grep -F '%l[' "$assembly" >/dev/null; then
    printf '%s\n' 'GNU asm-goto label placeholder leaked into emitted assembly' >&2
    exit 1
fi
if grep -F '%[ext]' "$assembly" >/dev/null; then
    printf '%s\n' 'GNU asm-goto named immediate placeholder leaked into emitted assembly' >&2
    exit 1
fi

printf '%s\n' \
    'PASS compiler/c0/gnu_inline_asm_goto labels=statement-id named-label=1 immediate-constant=33 dynamic-i=deferred-specialization target=RV64'
