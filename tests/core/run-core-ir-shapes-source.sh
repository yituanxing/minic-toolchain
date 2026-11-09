#!/usr/bin/env bash
set -Eeuo pipefail

minic=${1:?usage: run-core-ir-shapes-source.sh path/to/minic-cc}
root=$(cd "$(dirname "$0")/../.." && pwd)
source_file="$root/tests/core/core_ir_enum_source.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source_file"
cc -std=gnu11 -O0 -no-pie "$source_file" -o "$work/gnu.elf"
cc -E -P -x c "$source_file" -o "$work/source.i"
for target in x86_64 aarch64 loongarch64 riscv32 riscv64; do
    "$minic" --target="$target"-linux -S "$work/source.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
done
cc -no-pie -x assembler "$work/x86_64.s" -o "$work/mini.elf"
for impl in gnu mini; do
    set +e
    "$work/$impl.elf"
    actual=$?
    set -e
    test "$actual" -eq 42 || {
        printf 'Core enum source %s result=%s (expected 42)\n' \
            "$impl" "$actual" >&2
        exit 1
    }
done
echo 'CORE_IR_SHAPES_SOURCE=PASS GNU_C=1 five_ISA_S=5 x86_native=42'
