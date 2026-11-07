#!/usr/bin/env bash
# Core/ModuleIR fixed-point retention: ASM mnemonic -> late inline -> tail.
set -Eeuo pipefail
mini=${MINIC_CC:?set MINIC_CC to the minic-cc binary}
host=${HOST_CC:-cc}
source_file=tests/compiler/c0/core-late-live-closure.c
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$host" -std=gnu11 -E -P -x c "$source_file" -o "$work/mini.i"
"$host" -std=gnu11 -DGNU_KEEP_ASM_REFERENCED_STATIC=1 \
    -fno-pie -no-pie "$source_file" -o "$work/gnu"
"$work/gnu"

for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$mini" --target="$target" -S "$work/mini.i" \
        -o "$work/$target.s" 2>"$work/$target.log"
    grep -Eq 'MINIC_CORE_LATE_LOWER_CLOSURE rounds=2 functions=2' \
        "$work/$target.log"
    grep -Eq '^nop:' "$work/$target.s"
    grep -Eq '^late_tail:' "$work/$target.s"
    if grep -Eq '(^unrelated_dead:|^unresolved_dead_function:|[[:space:]]call[[:space:]]+unresolved_dead_function)' \
        "$work/$target.s"; then
        echo "MINIC_CORE_LATE_LIVE_CLOSURE=FAIL target=$target dead_function_retained" >&2
        exit 1
    fi
done

"$host" -fno-pie -no-pie -x assembler "$work/x86_64-linux.s" \
    -o "$work/mini"
"$work/mini"
"$host" -c -x assembler "$work/x86_64-linux.s" -o "$work/mini.o"
readelf -Ws "$work/mini.o" | grep -Eq '[[:space:]]nop$'
readelf -Ws "$work/mini.o" | grep -Eq '[[:space:]]late_tail$'
echo 'MINIC_CORE_LATE_LIVE_CLOSURE=PASS rounds=2 lowered=2 targets=5 native_gnu=1 native_minic=1 elf=1'
