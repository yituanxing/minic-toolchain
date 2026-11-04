#!/usr/bin/env bash
# MC-014 / MC-015: genuine GNU/MiniC RV32+RV64 ELF/QEMU differential.
# The callee uses an aligned(64) local while the caller deliberately presents
# all four legal 16-byte incoming-SP phases modulo 64. No libc or emulation
# shortcut: both .i compilers enter the same GNU assembler/linker and QEMU.
set -Eeuo pipefail

: "${MINIC_CC:?point MINIC_CC at the built MiniC compiler}"
target=${1:?usage: probe-riscv-overaligned-varargs-qemu.sh rv32|rv64}
case "$target" in
  rv32)
    xlen=32 march=rv32i mabi=ilp32 emulation=elf32lriscv
    gnu_cc=${RV32_GNU_CC:-riscv64-unknown-elf-gcc}
    qemu=${QEMU_RV32:-qemu-riscv32}
    minic_target=riscv32-linux
    ;;
  rv64)
    xlen=64 march=rv64i mabi=lp64 emulation=elf64lriscv
    gnu_cc=${RV64_GNU_CC:-riscv64-linux-gnu-gcc}
    qemu=${QEMU_RV64:-qemu-riscv64}
    minic_target=riscv64-linux
    ;;
  *) echo "RISCV_OVERALIGNED_VARARGS_QEMU=FAIL invalid_target=$target" >&2; exit 2 ;;
esac
gnu_ld=${RISCV_GNU_LD:-riscv64-linux-gnu-ld}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for tool in "$MINIC_CC" "$gnu_cc" "$gnu_ld" "$qemu"; do
  if [[ "$tool" == */* ]]; then
    [[ -x "$tool" ]] || { echo "RISCV_OVERALIGNED_VARARGS_QEMU=FAIL missing=$tool" >&2; exit 1; }
  else
    command -v "$tool" >/dev/null || { echo "RISCV_OVERALIGNED_VARARGS_QEMU=FAIL missing=$tool" >&2; exit 1; }
  fi
done

"$gnu_cc" -march="$march" -mabi="$mabi" -std=gnu11 -E -P \
  -x c "$root/tests/compiler/c0/riscv_overaligned_varargs.c" \
  -o "$work/callee.i"
"$gnu_cc" -march="$march" -mabi="$mabi" -std=gnu11 \
  -O0 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector \
  -c -x c "$work/callee.i" -o "$work/gnu-callee.o"
"$MINIC_CC" --target="$minic_target" -march="$march" -mabi="$mabi" \
  -S "$work/callee.i" -o "$work/minic-callee.s"
"$gnu_cc" -march="$march" -mabi="$mabi" -c \
  -x assembler "$work/minic-callee.s" -o "$work/minic-callee.o"

# Exact SP-phase forcing happens in the *caller* independently of either
# callee compiler. Its 16-byte outgoing stack area holds 2-3 overflow words.
# Save the real caller SP in s0, then anchor a scratch call area below it;
# GNU GAS's four invocations force entry SP%64 = 0,48,32,16 respectively.
cat > "$work/driver.S" <<'ASMSRC'
#if __riscv_xlen == 64
#define SAVE sd
#define LOAD ld
#define FRAME 32
#define RA_SLOT 24
#define S0_SLOT 16
#define WORD 8
#define TWO_WORD 16
#else
#define SAVE sw
#define LOAD lw
#define FRAME 16
#define RA_SLOT 12
#define S0_SLOT 8
#define WORD 4
#define TWO_WORD 8
#endif
    .section .text
    .balign 4
    .macro phase suffix, shift
    .globl phase_\suffix
    .type phase_\suffix, @function
phase_\suffix:
    addi sp, sp, -FRAME
    SAVE ra, RA_SLOT(sp)
    SAVE s0, S0_SLOT(sp)
    mv s0, sp
    addi sp, sp, -128
    andi sp, sp, -64
    addi sp, sp, -\shift

    # 1 named GPR: a1..a7 then stack at E, E+WORD.
    li a0, 100
    li a1, 1
    li a2, 2
    li a3, 3
    li a4, 4
    li a5, 5
    li a6, 6
    li a7, 7
    li t0, 8
    SAVE t0, 0(sp)
    li t0, 9
    SAVE t0, WORD(sp)
    call va_frame_one
    li t0, 145
    bne a0, t0, .Lfail_one\@

    # 7 named GPR: one saved register and two overflow words.
    li a0, 1
    li a1, 2
    li a2, 3
    li a3, 4
    li a4, 5
    li a5, 6
    li a6, 7
    li a7, 8
    li t0, 9
    SAVE t0, 0(sp)
    li t0, 10
    SAVE t0, WORD(sp)
    call va_frame_seven
    li t0, 55
    bne a0, t0, .Lfail_seven\@

    # 8 named GPR: no register save area, va_start must be E.
    li a0, 1
    li a1, 2
    li a2, 3
    li a3, 4
    li a4, 5
    li a5, 6
    li a6, 7
    li a7, 8
    li t0, 9
    SAVE t0, 0(sp)
    li t0, 10
    SAVE t0, WORD(sp)
    call va_frame_eight
    li t0, 55
    bne a0, t0, .Lfail_eight\@

    # 9 named arguments: E carries the last fixed argument; va_start must
    # skip it and start at E+WORD (GNU RISC-V va_list ABI).
    li a0, 1
    li a1, 2
    li a2, 3
    li a3, 4
    li a4, 5
    li a5, 6
    li a6, 7
    li a7, 8
    li t0, 9
    SAVE t0, 0(sp)
    li t0, 10
    SAVE t0, WORD(sp)
    li t0, 11
    SAVE t0, TWO_WORD(sp)
    call va_frame_nine
    li t0, 66
    bne a0, t0, .Lfail_nine\@

    # 2 named GPR plus a dynamic alloca inside the callee.
    li a0, 50
    li a1, 7
    li a2, 1
    li a3, 2
    li a4, 3
    li a5, 4
    li a6, 5
    li a7, 6
    li t0, 7
    SAVE t0, 0(sp)
    li t0, 8
    SAVE t0, WORD(sp)
    li t0, 9
    SAVE t0, TWO_WORD(sp)
    call va_frame_dynamic
    li t0, 102
    bne a0, t0, .Lfail_dynamic\@
    li a0, 0
    j .Lphase_end\@
.Lfail_one\@:
    li a0, 1
    j .Lphase_end\@
.Lfail_seven\@:
    li a0, 2
    j .Lphase_end\@
.Lfail_eight\@:
    li a0, 3
    j .Lphase_end\@
.Lfail_nine\@:
    li a0, 5
    j .Lphase_end\@
.Lfail_dynamic\@:
    li a0, 4
.Lphase_end\@:
    mv sp, s0
    LOAD s0, S0_SLOT(sp)
    LOAD ra, RA_SLOT(sp)
    addi sp, sp, FRAME
    ret
    .size phase_\suffix, .-phase_\suffix
    .endm

    phase 0, 0
    phase 16, 16
    phase 32, 32
    phase 48, 48

    .globl _start
    .type _start, @function
_start:
    call phase_0
    bnez a0, .Lexit
    call phase_16
    bnez a0, .Lexit
    call phase_32
    bnez a0, .Lexit
    call phase_48
.Lexit:
    li a7, 93
    ecall
    .size _start, .-_start
ASMSRC
"$gnu_cc" -march="$march" -mabi="$mabi" \
  -fno-pie -fno-pic -c -x assembler-with-cpp "$work/driver.S" \
  -o "$work/driver.o"

for mode in gnu minic; do
  "$gnu_ld" -m "$emulation" -static -e _start -o "$work/$mode.elf" \
    "$work/driver.o" "$work/$mode-callee.o"
  set +e
  timeout 30s "$qemu" "$work/$mode.elf"
  status=$?
  set -e
  if ((status != 0)); then
    echo "RISCV_OVERALIGNED_VARARGS_QEMU=FAIL target=$target mode=$mode status=$status" >&2
    exit 1
  fi
  echo "RISCV_OVERALIGNED_VARARGS_QEMU_MODE=PASS target=$target mode=$mode phases=4/4 functions=5/5"
done
echo "RISCV_OVERALIGNED_VARARGS_QEMU=PASS target=$target GNU=1 MiniC=1 GNU_AS=1 GNU_LD=1 QEMU=1 phases=4/4 tests=20/20"
