#!/bin/sh
set -eu
: "${MINIC_CC:?set MINIC_CC to the current minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/debug"}/tests/core-inline-statement-constant-guard
mkdir -p "$work"
source="$root/tests/compiler/c0/core_inline_statement_constant_guard.c"

cc -E -P -std=gnu11 -x c "$source" -o "$work/source.i"
cc -O2 -Werror -S -x c "$work/source.i" -o "$work/gnu.s"

for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    "$MINIC_CC" --target="$target" -S "$work/source.i" -o "$work/$target.s"
    if grep -Eq 'impossible_(request_index|dynamic_request)' "$work/$target.s"; then
        echo "CORE_INLINE_STATEMENT_CONSTANT_GUARD=FAIL $target retained an impossible pure guard" >&2
        exit 1
    fi
done

cc -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
cc -c -x assembler "$work/x86_64-linux.s" -o "$work/minic.o"
cc -no-pie -Wl,-z,noexecstack "$work/gnu.o" -o "$work/gnu"
cc -no-pie -Wl,-z,noexecstack "$work/minic.o" -o "$work/minic"
"$work/gnu"
"$work/minic"

printf 'CORE_INLINE_STATEMENT_CONSTANT_GUARD=PASS targets=5 GNU_O2=1 GNU_AS=2 GNU_LD=2 native_runtime=2 negative=volatile,call,asm,side_effecting_constant_false\n'
