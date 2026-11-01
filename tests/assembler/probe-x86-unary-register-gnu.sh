#!/usr/bin/env bash
# Exact GNU x86 MCInst register-operand byte oracle: one Target setter owns
# the invariant kind/register fields in unary and mixed memory instruction forms.
set -Eeuo pipefail
: "$MINIAS"
for tool in as objcopy cmp; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/unary.s" <<'SRC'
.text
.globl cleanup_registers
cleanup_registers:
  rdrand %r9
  rdseed %r10
  bswap %r8
  sete %al
  setne %r9b
  pushq %r8
  popq %r8
  movabsq $0x1122334455667788, %r8
  call *%r11
  jmp *%r12
  testb $7, %al
SRC
as --64 -o "$work/gnu.o" "$work/unary.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/unary.s"
for flavor in mini gnu; do
    objcopy -j .text -O binary "$work/$flavor.o" "$work/$flavor.bin"
done
cmp "$work/gnu.bin" "$work/mini.bin"
echo 'MINIAS_X86_UNARY_REGISTER=PASS GNU_text=exact forms=11'
