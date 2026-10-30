#!/usr/bin/env bash
# GNU GAS byte/relocation differential for already-defined SHN_ABS PUSH.
# A bound ordinary symbol must not be shortened speculatively; an absolute
# .set/.equ is a machine constant, so the same encoder chooses imm8 or imm32.
set -Eeuo pipefail
export LC_ALL=C

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT

cat >"$work/abs_push.s" <<'ASM'
    .text
    .set zero, 0
    .equ negative, -1
    .set small, 127
    .set large, 128
    .set sum, 9
    .globl abs_push
abs_push:
    pushq $zero
    pushq $negative
    pushq $small
    pushq $large
    pushq $sum+3
    pushq $-129
    ret
ASM

as --64 -o "$work/gnu.o" "$work/abs_push.s"
"$minias" --target=x86_64 -o "$work/mini.o" "$work/abs_push.s"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.bin"
if ! cmp -s "$work/gnu.bin" "$work/mini.bin"; then
    printf 'MINIAS_X86_ABS_PUSH_GNU=FAIL reason=text_bytes\n' >&2
    cmp -l "$work/gnu.bin" "$work/mini.bin" | head -12 >&2 || true
    exit 1
fi
if readelf -Wr "$work/gnu.o" | grep -q 'R_X86_64_' ||
   readelf -Wr "$work/mini.o" | grep -q 'R_X86_64_'; then
    printf 'MINIAS_X86_ABS_PUSH_GNU=FAIL reason=unexpected_absolute_relocation\n' >&2
    exit 1
fi
printf 'MINIAS_X86_ABS_PUSH_GNU=PASS GNU_bytes=exact ABS_relocations=zero\n'
