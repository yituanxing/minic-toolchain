#!/usr/bin/env bash
set -Eeuo pipefail

minic=${1:?usage: run-packed-bitfield-field-layout.sh path/to/minic-cc}
root=$(cd "$(dirname "$0")/../../.." && pwd)
source_file="$root/tests/compiler/c0/packed_bitfield_field_layout.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source_file"
cc -std=gnu11 -x c -no-pie "$source_file" -o "$work/gnu.elf"
cc -E -P -x c "$source_file" -o "$work/source.i"

for target in x86_64 aarch64 loongarch64 riscv32 riscv64; do
    "$minic" --target="$target"-linux -S "$work/source.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
done

cc -no-pie -x assembler "$work/x86_64.s" -o "$work/minic.elf"
for impl in gnu minic; do
    set +e
    "$work/$impl.elf"
    actual=$?
    set -e
    test "$actual" -eq 42 || {
        printf '%s packed bit-field native result: %s (expected 42)\n' \
            "$impl" "$actual" >&2
        exit 1
    }
done
echo 'PACKED_BITFIELD_FIELD_LAYOUT=PASS gcc_oracle=1 five_ISA_S=5 x86_native=42'
