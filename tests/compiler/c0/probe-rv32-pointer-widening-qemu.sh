#!/usr/bin/env bash
# MC-048: same GNU-preprocessed RV32 C -> GNU C / MiniC C -> GNU ELF32 -> QEMU.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
gnu_cc=${RV32_GNU_CC:-riscv64-linux-gnu-gcc}
gnu_ld=${RV32_GNU_LD:-riscv64-linux-gnu-ld}
qemu=${QEMU_RV32:-qemu-riscv32}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { echo "MC048_RV32_POINTER_WIDEN=FAIL $*" >&2; exit 1; }
test -x "$minic" || fail 'MiniC binary missing'

src="$root/tests/compiler/audit/mc048_rv32_pointer_widening.c"
# Intentional implementation-defined casts emit GCC warnings on RV32.
gcc_cast_warnings=(-Wno-pointer-to-int-cast -Wno-int-to-pointer-cast)
"$gnu_cc" -std=gnu11 -march=rv32i -mabi=ilp32 -Werror \
    "${gcc_cast_warnings[@]}" -E -P "$src" -o "$work/pointer.i" ||
    fail 'GNU RV32 preprocessing'
"$gnu_cc" -std=gnu11 -march=rv32i -mabi=ilp32 -Werror \
    "${gcc_cast_warnings[@]}" -ffreestanding -fno-builtin -fno-stack-protector \
    -O0 -c -x c "$work/pointer.i" -o "$work/pointer.gnu.o" ||
    fail 'GNU RV32 C compile'
"$minic" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/pointer.i" -o "$work/pointer.minic.s" ||
    fail 'MiniC RV32 -S'
"$gnu_cc" -march=rv32i -mabi=ilp32 \
    -c "$work/pointer.minic.s" -o "$work/pointer.minic.o" ||
    fail 'GNU RV32 AS MiniC output'
"$gnu_cc" -march=rv32i -mabi=ilp32 \
    -c "$root/tests/compiler/c0/mc048_rv32_linux_start.S" -o "$work/start.o" ||
    fail 'GNU RV32 AS _start'

# Same startup/ELF/QEMU conditions for both C compilers. The function-under-
# test returns 7 only if bit31=1 pointers sign-extend on direct casts, the
# intermediate unsigned long path zero-extends, and 64->ptr round trips hold.
for mode in gnu minic; do
    "$gnu_ld" -m elf32lriscv -static -e _start \
        -o "$work/$mode.elf" "$work/start.o" "$work/pointer.$mode.o" ||
        fail "GNU RV32 ELF32 link mode=$mode"
    status=0
    timeout 30s "$qemu" "$work/$mode.elf" || status=$?
    test "$status" -eq 7 || fail "GNU RV32 QEMU mode=$mode exit=$status expected=7"
    echo "MC048_RV32_POINTER_WIDEN_MODE=PASS mode=$mode GNU_ELF32=1 QEMU=1 exit=7"
done
echo 'MC048_RV32_POINTER_WIDEN=PASS GNU_MiniC_RV32I_ILP32_ELF32_QEMU_signed_unsigned_round_trip=7'
