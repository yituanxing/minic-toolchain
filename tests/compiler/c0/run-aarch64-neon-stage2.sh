#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu_cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
qemu=${QEMU_AARCH64:-qemu-aarch64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/aarch64-neon-stage2
mkdir -p "$work"

# The *same* GNU-preprocessed original C TU is both the layout oracle and
# MiniC's input. Do not substitute an integer or record type for a vector.
"$gnu_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_vector_layout.c" \
    -o "$work/layout.i"
"$minic_cc" --target=aarch64-linux -S "$work/layout.i" -o "$work/layout.minic.s"
"$gnu_cc" -static "$work/layout.minic.s" -o "$work/layout.minic.elf"
"$gnu_cc" -static -O2 \
    "$root/tests/compiler/c0/aarch64_neon_vector_layout.c" \
    -o "$work/layout.gnu.elf"
"$qemu" "$work/layout.gnu.elf"
"$qemu" "$work/layout.minic.elf"

# The source vocabulary is *target owned*: x86 must not inherit AArch64's
# builtin vector spelling or its 16-byte storage admission.
if "$minic_cc" --target=x86_64-linux -S "$work/layout.i" \
    -o "$work/layout.x86.s" >"$work/layout.x86.stdout" \
    2>"$work/layout.x86.stderr"; then
    printf '%s\n' 'FAIL AArch64 NEON layout leaked to x86 target' >&2
    exit 1
fi
grep -F 'expected type name' "$work/layout.x86.stderr" >/dev/null

# Independent GNU/QEMU semantic oracle for native SIMD register operations.
# MiniC is required to fail closed on GCC's target-specific builtin-header
# declaration until vector IR/ABI and real arm_neon.h support are finished.
"$gnu_cc" -O2 -static -ffreestanding \
    "$root/tests/compiler/c0/aarch64_neon_load_xor_store.c" \
    -o "$work/xor.gnu.elf"
"$qemu" "$work/xor.gnu.elf"
"$gnu_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_load_xor_store.c" \
    -o "$work/xor.i"
if "$minic_cc" --target=aarch64-linux -S "$work/xor.i" \
    -o "$work/xor.minic.s" >"$work/xor.minic.stdout" \
    2>"$work/xor.minic.stderr"; then
    printf '%s\n' 'FAIL MiniC silently admitted unfinished native SIMD lowering' >&2
    exit 1
fi
grep -F 'unsupported GCC pragma directive' "$work/xor.minic.stderr" >/dev/null

# A source-level vector object declaration and sizeof do not authorize either
# vector arithmetic or the by-value AAPCS64 V-register argument/return ABI.
for family in alu abi; do
    "$gnu_cc" -E -P -x c \
        "$root/tests/compiler/c0/aarch64_neon_unsupported_$family.c" \
        -o "$work/unsupported-$family.i"
    "$gnu_cc" -O2 -c \
        "$root/tests/compiler/c0/aarch64_neon_unsupported_$family.c" \
        -o "$work/unsupported-$family.gnu.o"
    if "$minic_cc" --target=aarch64-linux -S \
        "$work/unsupported-$family.i" -o "$work/unsupported-$family.minic.s" \
        >"$work/unsupported-$family.stdout" \
        2>"$work/unsupported-$family.stderr"; then
        printf '%s\n' "FAIL MiniC silently admitted unfinished vector $family" >&2
        exit 1
    fi
done
grep -F 'binary operator requires int operands' "$work/unsupported-alu.stderr" >/dev/null
grep -F 'unsupported function return type' "$work/unsupported-abi.stderr" >/dev/null

printf '%s\n' 'PASS AArch64 NEON stage2 layout=MiniC+GNU/QEMU(native) intrinsics=GNU/QEMU(reference) MiniC-intrinsic/ALU/ABI=honest-FAIL other-ISA=reject'
