#!/bin/sh
# Two authentic Linux 6.6.143 backend gaps: pxa i2c enum casts and
# fbcon_set_font's mixed-signedness GNU checked multiplication.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
host=${HOST_CC:-cc}
pointer_src=tests/compiler/c0/aarch64_linux_pointer_narrow_bitcast.c
overflow_src=tests/compiler/c0/loongarch64_linux_mixed_mul_overflow.c

"$host" -E -P -nostdinc "$pointer_src" -o "$work/pointer.i"
"$host" -E -P -nostdinc "$overflow_src" -o "$work/overflow.i"
"$host" -E -P -nostdinc tests/compiler/c0/pointer_integer_casts.c \
    -o "$work/pointer-roundtrip.i"
"$MINIC_CC" --target=aarch64-linux -fPIE -S "$work/pointer.i" -o "$work/pointer.s"
"$MINIC_CC" --target=aarch64-linux -fPIE -S "$work/overflow.i" -o "$work/overflow.s"

# 32-bit pointer casts must explicitly sign/zero extend the low word.
# 64-bit casts preserve the address; _Bool remains canonical 0/1.
test "$(grep -Fc '  sxtw x9, w9' "$work/pointer.s")" -ge 2
test "$(grep -Fc '  uxtw x9, w9' "$work/pointer.s")" -ge 1
grep -Fq '  cmp x9, #0' "$work/pointer.s"
grep -Fq '  cset x9, ne' "$work/pointer.s"

# Widen before multiplying, then compare the exact 64-bit product to the
# destination's 32-bit sign/zero extension, not C-promoted 32-bit operands.
test "$(grep -Fc '  mul x12, x9, x10' "$work/overflow.s")" -eq 3
test "$(grep -Fc '  sxtw x13, w9' "$work/overflow.s")" -eq 1
test "$(grep -Fc '  uxtw x13, w9' "$work/overflow.s")" -eq 2
test "$(grep -Fc '  cmp x12, x13' "$work/overflow.s")" -eq 3
echo 'MINIC_AARCH64_LINUX_NARROW_PTR_MIXED_MUL=PASS ptr_enum_i32=1 ptr_i32=1 ptr_u32=1 mixed_mul=3'

# GCC is the oracle for the exact GNU conversion and checked arithmetic.
"$host" -Wno-pointer-to-int-cast -DMINI_POINTER_NARROW_NATIVE_TEST -O2 \
    "$pointer_src" -o "$work/pointer_native"
"$work/pointer_native"
"$host" -DMINI_MIXED_OVERFLOW_HOST_TEST -O2 "$overflow_src" \
    -o "$work/overflow_native"
"$work/overflow_native"

# A direct pointer-to-float cast is invalid and must remain rejected by both
# GNU and MiniC on each target, despite expanding integer bitcast width.
"$host" -E -P -nostdinc -DMINI_POINTER_NARROW_NEGATIVE \
    "$pointer_src" -o "$work/invalid.i"
if "$host" -Wno-pointer-to-int-cast -fsyntax-only -x c "$work/invalid.i" \
    >"$work/gnu-negative.log" 2>&1; then
    echo 'GNU unexpectedly accepted pointer-to-float' >&2
    exit 1
fi
for target in aarch64-linux loongarch64-linux x86_64-linux riscv64-linux riscv32-linux; do
    "$MINIC_CC" "--target=$target" -S "$work/pointer-roundtrip.i" \
        -o "$work/roundtrip-$target.s"
    if "$MINIC_CC" "--target=$target" -S "$work/invalid.i" \
        -o "$work/invalid-$target.s" >"$work/invalid-$target.log" 2>&1; then
        echo "MiniC unexpectedly accepted pointer-to-float target=$target" >&2
        exit 1
    fi
done
echo 'MINIC_AARCH64_LINUX_NARROW_PTR_MIXED_MUL_GNU_ORACLE=PASS pointer_samples=7 mixed_pairs=72 positive_targets=5 negative_targets=5'

cross=${AARCH64_CC:-aarch64-linux-gnu-gcc}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/pointer.s" -o "$work/pointer.o"
    "$cross" -c -x assembler "$work/overflow.s" -o "$work/overflow.o"
    echo 'MINIC_AARCH64_LINUX_NARROW_PTR_MIXED_MUL_GNU_AS=PASS'
else
    echo 'MINIC_AARCH64_LINUX_NARROW_PTR_MIXED_MUL_GNU_AS=SKIP missing AArch64 GNU assembler'
fi
