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

# The former ALU negative case now has genuine 128-bit Core/Q-register XOR
# lowering. Its C function ABI is still scalar pointers, not vector by value.
"$gnu_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_unsupported_alu.c" \
    -o "$work/alu.i"
"$gnu_cc" -O2 -c \
    "$root/tests/compiler/c0/aarch64_neon_unsupported_alu.c" \
    -o "$work/alu.gnu.o"
"$minic_cc" --target=aarch64-linux -S "$work/alu.i" \
    -o "$work/alu.minic.s"
grep -F '  eor v16.16b, v16.16b, v17.16b' "$work/alu.minic.s" >/dev/null
"$gnu_cc" -c "$work/alu.minic.s" -o "$work/alu.minic.o"

# AAPCS64 Q-register by-value function ABI is independently typed and
# transported through q0 rather than integer GPR pairs.
"$gnu_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_unsupported_abi.c" \
    -o "$work/unsupported-abi.i"
"$gnu_cc" -O2 -c \
    "$root/tests/compiler/c0/aarch64_neon_unsupported_abi.c" \
    -o "$work/unsupported-abi.gnu.o"
"$minic_cc" --target=aarch64-linux -S \
    "$work/unsupported-abi.i" -o "$work/unsupported-abi.minic.s" \
    >"$work/typed-abi.stdout" 2>"$work/typed-abi.stderr"
grep -F '  str q0,' "$work/unsupported-abi.minic.s" >/dev/null
grep -F '  ldr q0,' "$work/unsupported-abi.minic.s" >/dev/null
"$gnu_cc" -c "$work/unsupported-abi.minic.s" -o "$work/typed-abi.minic.o"

# Full D-vector ABI execution: true 8B values, by-value direct/indirect
# calls and the ninth SIMD argument spilling to AAPCS64's stack overflow.
"$gnu_cc" -E -P -x c \
    "$root/tests/compiler/c0/aarch64_neon_vector64_abi.c" \
    -o "$work/d-vector-abi.i"
"$gnu_cc" -static -O2 \
    "$root/tests/compiler/c0/aarch64_neon_vector64_abi.c" \
    -o "$work/d-vector-abi.gnu.elf"
"$qemu" "$work/d-vector-abi.gnu.elf"
"$minic_cc" --target=aarch64-linux -S \
    "$work/d-vector-abi.i" -o "$work/d-vector-abi.minic.s"
"$gnu_cc" -static "$work/d-vector-abi.minic.s" \
    -o "$work/d-vector-abi.minic.elf"
"$qemu" "$work/d-vector-abi.minic.elf"

printf '%s\n' 'PASS AArch64 NEON stage2 layout=MiniC+GNU/QEMU ALU=MiniC-Q-object Q-ABI=typed D-ABI=GNU/MiniC-QEMU intrinsics=GNU/QEMU(reference) MiniC-full-header=honest-FAIL other-ISA=reject'
