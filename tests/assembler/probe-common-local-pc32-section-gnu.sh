#!/usr/bin/env bash
# Real x86 Linux cross-section local PC32 relocations are canonicalized by
# GNU GAS to STT_SECTION plus local offset; ELF32 REL stores that addend in
# the section field, ELF64 RELA stores it in the relocation record. A link
# may happen to resolve both forms, but the assembler's object contract is
# stronger: require the same GNU relocation target and effective addend.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in as objcopy readelf cmp python3; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/local-pc32.s" <<'ASM'
.section .text.entry,"ax",@progbits
.globl entry
.type entry,@function
entry:
    call local_after_padding
    .long local_after_padding - .
    .long local_data - .
    .long .Lhidden_after - .
    ret
.section .text.padding,"ax",@progbits
.zero 13
local_after_padding:
    ret
.Lhidden_after:
    ret
.section .rodata.data,"a",@progbits
.zero 44
local_data:
    .long 0x11223344
ASM

for width in 32 64; do
    if [[ $width == 32 ]]; then
        target=(-march=i386 -mabi=i386)
    else
        target=()
    fi
    as --"$width" -o "$work/gnu.o" "$work/local-pc32.s"
    "$MINIAS" --target=x86_64 "${target[@]}" \
        -o "$work/mini.o" "$work/local-pc32.s"
    for section in .text.entry .text.padding .rodata.data; do
        objcopy --dump-section "$section=$work/gnu.bin" "$work/gnu.o"
        objcopy --dump-section "$section=$work/mini.bin" "$work/mini.o"
        if ! cmp "$work/gnu.bin" "$work/mini.bin"; then
            printf 'MISMATCH section=%s ELF%s\nGNU:\n' "$section" "$width" >&2
            readelf -rW "$work/gnu.o" >&2
            printf 'MiniAS:\n' >&2
            readelf -rW "$work/mini.o" >&2
            exit 1
        fi
    done
    if [[ $width == 64 ]]; then
        python3 tests/assembler/elf_semantic_compare.py --strict \
            "$work/gnu.o" "$work/mini.o"
    else
        readelf -rW "$work/gnu.o" | awk '$3 ~ /^R_/ {print $1,$3,$5,$6,$7}' \
            >"$work/gnu.rels"
        readelf -rW "$work/mini.o" | awk '$3 ~ /^R_/ {print $1,$3,$5,$6,$7}' \
            >"$work/mini.rels"
        if ! cmp "$work/gnu.rels" "$work/mini.rels"; then
            echo 'GNU relocation targets:' >&2
            readelf -rW "$work/gnu.o" >&2
            echo 'MiniAS relocation targets:' >&2
            readelf -rW "$work/mini.o" >&2
            exit 1
        fi
    fi
    echo "MINIAS_LOCAL_PC32_GNU=PASS ELF${width} local_section_targets=exact implicit_addend=exact"
done
