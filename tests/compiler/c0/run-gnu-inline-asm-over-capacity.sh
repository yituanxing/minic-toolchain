#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/release"}/tests/gnu-inline-asm-over-capacity
mkdir -p "$work"
fixture="$root/tests/compiler/c0/gnu_inline_asm_over_capacity.c"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$fixture"
"$host_cc" -std=gnu11 -E -P -x c "$fixture" -o "$work/over-capacity.i"
for isa in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    if "$minic" --target="$isa" -S "$work/over-capacity.i" \
        -o "$work/$isa.s" 2>"$work/$isa.stderr"; then
        printf 'FAIL: %s accepted 17 data operands\n' "$isa" >&2
        exit 1
    fi
    # The rejection is a Core capacity decision, never a frontend syntax
    # error, a backend crash, or silently truncated assembly.
    if grep -E 'expected|unexpected token|parse error|syntax error' \
        "$work/$isa.stderr" >/dev/null; then
        printf 'FAIL: %s rejected valid GNU syntax in the parser\n' "$isa" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS GNU 17-operand rejection five ISAs, GNU syntax oracle accepted'
