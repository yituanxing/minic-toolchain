#!/usr/bin/env bash
# GNU exact instruction bytes after shared x86 operand/MCLayout cleanup.
set -Eeuo pipefail
: "$MINIAS"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for t in as objcopy cmp; do command -v "$t" >/dev/null; done
cat >"$work/pair.s" <<'SRC'
.text
.globl register_pair_check
register_pair_check:
  movq %rax, %xmm1
  movq %xmm1, %rax
  bsf %r8, %r9
  bsr %eax, %edx
  imulq %r10, %r11
  popcntq %r9, %r10
  lzcntl %eax, %edx
  tzcntq %r8, %r9
  addq %r10, %r11
  xaddq %r10, %r11
  xchgq %rax, %r13
  .p2align 4,,4
  .byte 0x7f
  .balign 8,,3
  .byte 0x42
SRC
as --64 -o "$work/gnu.o" "$work/pair.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/pair.s"
for name in gnu mini; do
    objcopy -j .text -O binary "$work/$name.o" "$work/$name.bin"
done
cmp "$work/gnu.bin" "$work/mini.bin"
echo 'MINIAS_X86_PAIR_ALIGNMENT=PASS GNU_text=exact register_families=11 max_skip=2'
