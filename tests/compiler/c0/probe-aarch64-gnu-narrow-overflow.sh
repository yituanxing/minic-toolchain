#!/bin/sh
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file=tests/compiler/c0/aarch64_linux_overflow_narrow_predicate.c

"$host" -E -P -nostdinc "$source_file" -o "$work/narrow.i"
"$MINIC_CC" --target=aarch64-linux -fPIE -S \
    "$work/narrow.i" -o "$work/narrow.s"

test "$(grep -Fc '  cmp x12, x13' "$work/narrow.s")" -eq 12
test "$(grep -Fc '  strb w9, [x11]' "$work/narrow.s")" -eq 6
test "$(grep -Fc '  strh w9, [x11]' "$work/narrow.s")" -eq 6
test "$(grep -Fc '  add x12, x9, x10' "$work/narrow.s")" -eq 4
test "$(grep -Fc '  sub x12, x9, x10' "$work/narrow.s")" -eq 4
test "$(grep -Fc '  mul x12, x9, x10' "$work/narrow.s")" -eq 4

"$host" -DMINIC_OVERFLOW_NARROW_NATIVE_TEST -O2 "$source_file" \
    -o "$work/gnu-oracle"
"$work/gnu-oracle"
echo 'MINIC_AARCH64_GNU_NARROW_OVERFLOW=PASS signed_unsigned=12 native_pairs=436'

# Negative and positive target matrix: all five ISAs must preserve their
# already-supported uniform checked i32/i64 arithmetic and reject a C-invalid
# overflow result pointer (the AArch-specific narrow allowance is separate).
"$host" -E -P -nostdinc tests/compiler/c0/core_integer_multiply_overflow.c \
    -o "$work/uniform.i"
"$host" -E -P -nostdinc -DMINIC_OVERFLOW_NARROW_INVALID \
    "$source_file" -o "$work/invalid.i"
if "$host" -fsyntax-only -x c "$work/invalid.i" \
    >"$work/gnu-invalid.log" 2>&1; then
    echo 'GNU unexpectedly accepted boolean overflow destination' >&2
    exit 1
fi
for target in aarch64-linux loongarch64-linux x86_64-linux riscv64-linux riscv32-linux; do
    "$MINIC_CC" "--target=$target" -S "$work/uniform.i" \
        -o "$work/uniform-$target.s"
    if "$MINIC_CC" "--target=$target" -S "$work/invalid.i" \
        -o "$work/invalid-$target.s" >"$work/invalid-$target.log" 2>&1; then
        echo "MiniC unexpectedly accepted invalid bool target=$target" >&2
        exit 1
    fi
done
echo 'MINIC_AARCH64_GNU_NARROW_OVERFLOW_5ISA=PASS uniform_positive=5 bool_negative=5'
