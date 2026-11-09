#!/usr/bin/env bash
# Linux 6.6 ZSTD and hibernation LZO index large records, whose stride cannot
# be truncated to an AArch64 MOVZ 16-bit immediate. Check native GNU behavior,
# full AArch64 64-bit immediate materialization and five-ISA admission rules.
set -Eeuo pipefail

test "$#" -eq 1
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=$1
test -x "$minic" || {
    echo "expected executable MiniC-cc argument" >&2
    exit 1
}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

small="$root/tests/compiler/c0/aarch64_large_pointer_stride.c"
huge="$root/tests/compiler/c0/aarch64_64bit_pointer_stride.c"
invalid="$root/tests/compiler/c0/aarch64_large_pointer_stride_invalid.c"
cc -std=gnu11 -Wall -Wextra -Werror -O0 "$small" -o "$work/gnu.elf"
cc -std=gnu11 -Wall -Wextra -Werror -O0 -S "$huge" -o "$work/gnu-huge.s"
cc -E -P -x c "$small" -o "$work/small.i"
cc -E -P -x c "$huge" -o "$work/huge.i"
cc -E -P -x c "$invalid" -o "$work/invalid.i"
if cc -std=gnu11 -Werror -S "$invalid" -o "$work/gnu-invalid.s" \
    >"$work/gnu-invalid.out" 2>"$work/gnu-invalid.err"; then
    echo "GNU accepted noninteger pointer index unexpectedly" >&2
    exit 1
fi

for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$minic" --target="$target-linux" -S "$work/small.i" \
        -o "$work/small-$target.s"
    if "$minic" --target="$target-linux" -S "$work/invalid.i" \
        -o "$work/invalid-$target.s" >"$work/invalid-$target.out" \
        2>"$work/invalid-$target.err"; then
        echo "unexpected $target float pointer-index acceptance" >&2
        exit 1
    fi
    grep -F 'unsupported pointer arithmetic operands' \
        "$work/invalid-$target.err" >/dev/null
    test ! -s "$work/invalid-$target.s"
done

# 0x10120-byte element: AArch64 must materialize and use the high 16 bits.
grep -F '  movz x11, #288' "$work/small-aarch64.s" >/dev/null
grep -F '  movk x11, #1, lsl #16' "$work/small-aarch64.s" >/dev/null
grep -F '  mul x10, x10, x11' "$work/small-aarch64.s" >/dev/null
grep -F '  sub x9, x9, x10' "$work/small-aarch64.s" >/dev/null

# 0x100010120-byte element is representable on LP64 only. AArch64 needs both
# bit-16 and bit-32 MOVK, and RV32 must refuse the unrepresentable stride.
for target in riscv64 x86_64 aarch64 loongarch64; do
    "$minic" --target="$target-linux" -S "$work/huge.i" \
        -o "$work/huge-$target.s"
done
if "$minic" --target=riscv32-linux -S "$work/huge.i" \
    -o "$work/huge-riscv32.s" >"$work/rv32.out" 2>"$work/rv32.err"; then
    echo "RV32 accepted an unrepresentable >4GiB pointer stride" >&2
    exit 1
fi
test ! -s "$work/huge-riscv32.s"
grep -F '  movz x11, #288' "$work/huge-aarch64.s" >/dev/null
grep -F '  movk x11, #1, lsl #16' "$work/huge-aarch64.s" >/dev/null
grep -F '  movk x11, #1, lsl #32' "$work/huge-aarch64.s" >/dev/null

# Execute both GNU C and MiniC->GNU AS/LD natively on the same safe
# three-element array. No unallocated huge object participates in runtime.
cc -O0 -no-pie "$work/small-x86_64.s" -o "$work/minic.elf"
"$work/gnu.elf"
"$work/minic.elf"
echo "MINIC_AARCH64_LARGE_POINTER_STRIDE=PASS five_isa=5 width16=complete width32=complete RV32_4GiB=reject GNU_native=1 MiniC_x86_native=1"
