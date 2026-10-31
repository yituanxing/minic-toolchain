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

# GNU production GCC zlib uses `push $.LC0` for a .rodata string address.
# A local section label that has already been defined has a temporary
# within-section value (often zero), but it is NOT an absolute SHN_ABS
# machine constant. PUSH must retain imm32 + R_X86_64_32S, never 6A 00.
cat >"$work/section_push.s" <<'ASM'
    .section .rodata.str1.1,"aMS",@progbits,1
.LC_before:
    .asciz "1.3.1"
    .text
    .globl section_push
section_push:
    pushq $.LC_before
    pushq $.LC_before+1
    pushq $.LC_after
    pushq $external_string
    ret
    .section .rodata.str1.1,"aMS",@progbits,1
.LC_after:
    .asciz "future"
ASM
as --64 -o "$work/section-gnu.o" "$work/section_push.s"
"$minias" --target=x86_64 -o "$work/section-mini.o" "$work/section_push.s"
for obj in gnu mini; do
    objcopy -j .text -O binary "$work/section-$obj.o" "$work/section-$obj.bin"
    readelf -Wr "$work/section-$obj.o" | awk '
        /R_X86_64_32S/ {
            # GNU may fold a same-section .L local to the section symbol;
            # MiniAS can retain its local symbol. Both forms must remain
            # relocatable and resolve to identical bytes under GNU ld.
            identity=$5 == "external_string" ? "external" : "local_rodata"
            print $1 ":" $3 ":" identity
        }
    ' >"$work/section-$obj.rel"
done
if ! cmp -s "$work/section-gnu.bin" "$work/section-mini.bin"; then
    printf 'MINIAS_X86_SECTION_PUSH_GNU=FAIL reason=text_bytes\n' >&2
    cmp -l "$work/section-gnu.bin" "$work/section-mini.bin" | head -12 >&2 || true
    exit 1
fi
if [[ $(wc -l <"$work/section-gnu.rel") -ne 4 ]] ||
   ! cmp -s "$work/section-gnu.rel" "$work/section-mini.rel"; then
    printf 'MINIAS_X86_SECTION_PUSH_GNU=FAIL reason=ABS32S_relocations\n' >&2
    printf 'GNU:\n' >&2; sed -n '1,12p' "$work/section-gnu.rel" >&2
    printf 'MiniAS:\n' >&2; sed -n '1,12p' "$work/section-mini.rel" >&2
    exit 1
fi
for obj in gnu mini; do
    ld -e section_push --defsym=external_string=0x12345 \
        -o "$work/section-$obj.elf" "$work/section-$obj.o"
    objcopy -j .text -O binary "$work/section-$obj.elf" \
        "$work/section-$obj.linked.bin"
done
if ! cmp -s "$work/section-gnu.linked.bin" "$work/section-mini.linked.bin"; then
    printf 'MINIAS_X86_SECTION_PUSH_GNU=FAIL reason=GNU_linked_bytes\n' >&2
    exit 1
fi
printf 'MINIAS_X86_SECTION_PUSH_GNU=PASS GNU_bytes=exact ABS32S_relocations=4 GNU_link=exact\n'
