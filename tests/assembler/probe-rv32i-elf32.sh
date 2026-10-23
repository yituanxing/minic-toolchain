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
