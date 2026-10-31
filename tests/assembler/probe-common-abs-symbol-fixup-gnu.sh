#!/usr/bin/env bash
# Linux's x86 relocs validator rejects R_X86_64_32S against absolute local
# `.set` counters. GAS folds known SHN_ABS operands into the instruction
# field instead of publishing an unnecessary relocation. Compare a real
# collection of x86 addressing forms with the same GNU-only object stage.
set -Eeuo pipefail
: "${MINIAS:?MiniAS binary required}"
for tool in as objcopy readelf cmp; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/abs.s" <<'ASM'
.set i, 0
.text
.globl probe
probe:
    mov $i, %rax
    mov i(%rip), %eax
    mov i, %eax
    pushq $i
    movl $i, (%rsp)
    lea i(%rip), %rdi
    ret
ASM
as --64 -o "$work/gnu.o" "$work/abs.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/abs.s"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.bin"
if ! cmp "$work/gnu.bin" "$work/mini.bin"; then
    objdump -drw "$work/gnu.o" >&2
    objdump -drw "$work/mini.o" >&2
    exit 1
fi
! readelf -rW "$work/gnu.o" | grep -q 'R_X86_64'
! readelf -rW "$work/mini.o" | grep -q 'R_X86_64'
echo 'MINIAS_X86_ABS_SET_FIXUPS=PASS GNU_text=exact GNU_reloc=none'
