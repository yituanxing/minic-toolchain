#!/usr/bin/env bash
# RV32I ILP32 real assembler qualification: MiniAS ELF32 must match GNU AS,
# after pseudo expansion, ISA legality and RISC-V arch attributes.
set -Eeuo pipefail
: "${MINIAS:?MINIAS path required}"
: "${RISCV_GCC:=riscv64-linux-gnu-gcc}"
: "${RISCV_OBJCOPY:=riscv64-linux-gnu-objcopy}"
: "${RISCV_READELF:=riscv64-linux-gnu-readelf}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/legal.s" <<'SRC'
.text
.globl rv32_arith
.type rv32_arith, @function
rv32_arith:
    addi a0, a0, 5
    lw a1, 0(a0)
    slli a1, a1, 31
    srli a1, a1, 31
    sw a1, 0(a0)
    ret
.size rv32_arith, .-rv32_arith
SRC
"$MINIAS" -march=rv32i -mabi=ilp32 -o "$work/native.o" "$work/legal.s"
"$RISCV_GCC" -march=rv32i -mabi=ilp32 -x assembler -c \
  "$work/legal.s" -o "$work/reference.o"
"$RISCV_READELF" -h "$work/native.o" > "$work/header.txt"
"$RISCV_READELF" -A "$work/native.o" > "$work/attributes.txt"
grep -Eq 'Class:[[:space:]]+ELF32' "$work/header.txt"
grep -Eq 'Machine:[[:space:]]+RISC-V' "$work/header.txt"
grep -F 'rv32i2p1' "$work/attributes.txt"
"$RISCV_OBJCOPY" -j .text -O binary "$work/native.o" "$work/native.bin"
"$RISCV_OBJCOPY" -j .text -O binary "$work/reference.o" "$work/reference.bin"
cmp "$work/native.bin" "$work/reference.bin"
# RV32I li must use the RV32 base-I LUI/ADDI sequence, NEVER RV64 ADDIW
# or accidental 64-bit shifts. Check exact GNU bits at signedness edges.
cat >"$work/imm.s" <<'SRC'
.text
.globl rv32_immediate
rv32_immediate:
    li a0, 0x80000000
    li a1, 0xffffffff
    li a2, 2048
    li a3, -2049
    li a4, 0x12345000
    li a5, 0x7fffffff
    ret
SRC
"$MINIAS" -march=rv32i -mabi=ilp32 -o "$work/imm-mini.o" "$work/imm.s"
"$RISCV_GCC" -march=rv32i -mabi=ilp32 -x assembler -c \
    "$work/imm.s" -o "$work/imm-gcc.o"
"$RISCV_OBJCOPY" -j .text -O binary "$work/imm-mini.o" "$work/imm-mini.bin"
"$RISCV_OBJCOPY" -j .text -O binary "$work/imm-gcc.o" "$work/imm-gcc.bin"
cmp "$work/imm-mini.bin" "$work/imm-gcc.bin"
printf '.text\nli a0, 0x100000000\n' >"$work/outofrange.s"
if "$MINIAS" -march=rv32i -mabi=ilp32 \
     -o "$work/outofrange.o" "$work/outofrange.s" \
     >"$work/outofrange.out" 2>"$work/outofrange.err"; then
    echo 'RV32I_LI=FAIL accepted_overflow_32bit_imm' >&2; exit 1
fi
grep -F 'rv32i-li-overflows-32-bits' "$work/outofrange.err"
echo 'RV32I_LI=PASS GNU_AS_bytes_equal=true signed_unsigned_edges=6 overflow=reject'
# Execute an actual freestanding RV32 Linux ELF32 binary: no compiler-runtime
# or sysroot dependency, and this checks binutils' ELF32 input contract.
cat >"$work/exit.s" <<'SRC'
.text
.globl _start
_start:
    addi a0, zero, 0
    addi a7, zero, 93
    ecall
SRC
"$MINIAS" -march=rv32i -mabi=ilp32 -o "$work/exit.o" "$work/exit.s"
riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/exit.elf" "$work/exit.o"
qemu-riscv32 "$work/exit.elf"
echo 'RV32I_QEMU=PASS true_ELF32=1 linux_exit_syscall=0 no_libc=1'
for bad in 'ld a0, 0(a1)' 'sd a0, 0(a1)' 'addiw a0, a0, 1' \
           'addw a0, a0, a1' 'slli a0, a1, 32' 'srli a0, a1, 40' \
           'mul a0, a1, a2' 'csrr a0, sstatus'; do
  printf '.text\n%s\n' "$bad" >"$work/bad.s"
  if "$MINIAS" -march=rv32i -mabi=ilp32 \
       -o "$work/bad.o" "$work/bad.s" 2>"$work/diagnostic"; then
    echo "RV32I_MACHINE=FAIL accepted=$bad" >&2
    exit 1
  fi
done
if "$MINIAS" -march=rv32i -mabi=lp64 \
   -o "$work/mismatch.o" "$work/legal.s" 2>"$work/diagnostic"; then
  echo 'RV32I_MACHINE=FAIL mismatched_ABI_accepted' >&2
  exit 1
fi
echo 'RV32I_MACHINE=PASS ELF32_ILP32=1 gnu_AS_machine_bytes=equal arch_attribute=rv32i2p1 RV64_opcodes=blocked M_Zicsr=blocked'
