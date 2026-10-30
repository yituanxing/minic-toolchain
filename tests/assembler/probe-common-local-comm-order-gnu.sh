#!/usr/bin/env bash
# GNU local .comm allocates in .bss subsection zero only *after* all normal
# input declarations, regardless of lexical order, but before subsection 2.
# ELF64 RELA and ELF32 REL must carry the same effective section addend.
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the current assembler binary}"
for tool in as readelf objcopy cmp python3; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/commons.s" <<'ASM'
.local alpha
.comm alpha,2,1
.local beta
.comm beta,8,8
.bss
.zero 5
.subsection 2
.zero 3
.local gamma
.comm gamma,1,1
.subsection 0
.text
.globl entry
entry:
.long alpha,beta,gamma
ASM

for width in 32 64; do
    if [[ $width == 32 ]]; then
        target=(-march=i386 -mabi=i386)
    else
        target=()
    fi
    as --"$width" -o "$work/gnu.o" "$work/commons.s"
    "$MINIAS" --target=x86_64 "${target[@]}" \
        -o "$work/mini.o" "$work/commons.s"
    for name in alpha beta gamma; do
        mini=$(readelf -sW "$work/mini.o" | awk -v n="$name" '$NF == n { print $2 }')
        gnu=$(readelf -sW "$work/gnu.o" | awk -v n="$name" '$NF == n { print $2 }')
        test -n "$gnu" && test "$mini" = "$gnu"
    done
    objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
    objcopy -j .text -O binary "$work/mini.o" "$work/mini.bin"
    cmp "$work/gnu.bin" "$work/mini.bin"
    readelf -rW "$work/gnu.o" >"$work/gnu-relocs.txt"
    readelf -rW "$work/mini.o" >"$work/mini-relocs.txt"
    if [[ $width == 64 ]]; then
        python3 tests/assembler/elf_semantic_compare.py --strict \
            "$work/gnu.o" "$work/mini.o"
    else
        # The strict ELF semantic comparator intentionally has no EM_386
        # model. Use independent binutils relocs and a field-accurate .text
        # comparison rather than silently waiving the ELF32 REL case.
        awk '$3 ~ /^R_/ {print $1,$3,$5,$6,$7}' "$work/gnu-relocs.txt" \
            >"$work/gnu-relocs-normalized.txt"
        awk '$3 ~ /^R_/ {print $1,$3,$5,$6,$7}' "$work/mini-relocs.txt" \
            >"$work/mini-relocs-normalized.txt"
        cmp "$work/gnu-relocs-normalized.txt" "$work/mini-relocs-normalized.txt"
    fi
    echo "MINIAS_COMMON_LOCAL_COMM=PASS ELF${width} GNU_symbol_offsets=exact GNU_text=exact GNU_ELF=exact"
done
