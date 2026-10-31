#!/usr/bin/env bash
# AArch64 PC-relative LDR must preserve same-section GLOBAL/WEAK/HIDDEN
# symbol identity. Only a defined same-section LOCAL reference can be folded;
# a later strong definition may override a weak literal during GNU linkage.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${AARCH64_AS:=aarch64-linux-gnu-as}"
: "${AARCH64_LD:=aarch64-linux-gnu-ld}"
: "${AARCH64_OBJCOPY:=aarch64-linux-gnu-objcopy}"
for tool in "$AARCH64_AS" "$AARCH64_LD" "$AARCH64_OBJCOPY" readelf awk cmp; do
    command -v "$tool" >/dev/null
done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source="$here/fixtures/aarch64-ldr-binding-gnu.S"
strong="$here/fixtures/aarch64-ldr-binding-strong-gnu.S"

text_bytes() {
    "$AARCH64_OBJCOPY" -j .text -O binary "$1" "$2"
}

text_relocs() {
    readelf -Wr "$1" | awk '
        /^Relocation section / {
            inside = /\.rela\.text\x27/;
            next;
        }
        inside && /R_AARCH64_/ {
            print $1, $3, $5, $6, $7;
        }
    '
}

"$AARCH64_AS" -o "$work/gnu.o" "$source"
"$MINIAS" --target=aarch64 -o "$work/mini.o" "$source"
"$AARCH64_AS" -o "$work/strong.o" "$strong"
for flavor in gnu mini; do
    text_bytes "$work/$flavor.o" "$work/$flavor.text"
    text_relocs "$work/$flavor.o" >"$work/$flavor.reloc"
done
cmp "$work/gnu.text" "$work/mini.text" || {
    echo 'MINIAS_A64_LDR_BINDING=FAIL object_text' >&2
    exit 1
}
cmp "$work/gnu.reloc" "$work/mini.reloc" || {
    echo 'MINIAS_A64_LDR_BINDING=FAIL relocations' >&2
    diff -u "$work/gnu.reloc" "$work/mini.reloc" >&2 || true
    exit 1
}
if test "$(wc -l <"$work/mini.reloc")" -ne 5; then
    echo 'MINIAS_A64_LDR_BINDING=FAIL expected_four_loads_and_one_pool_reloc' >&2
    exit 1
fi
for expected in global_literal weak_literal external_literal; do
    grep -q "R_AARCH64_LD_PREL_LO19 $expected + 0" "$work/mini.reloc" || {
        echo "MINIAS_A64_LDR_BINDING=FAIL symbol=$expected" >&2
        exit 1
    }
done
grep -q 'R_AARCH64_LD_PREL_LO19 hidden_literal + 8' "$work/mini.reloc" || {
    echo 'MINIAS_A64_LDR_BINDING=FAIL hidden_global_addend' >&2
    exit 1
}
if test "$(grep -c 'R_AARCH64_LD_PREL_LO19 ' "$work/mini.reloc")" -ne 4 ||
   ! grep -q 'R_AARCH64_ABS64 global_literal + 0' "$work/mini.reloc"; then
    echo 'MINIAS_A64_LDR_BINDING=FAIL pool_not_private_or_instruction_fixup' >&2
    exit 1
fi
if grep -q '\.Llocal_literal' "$work/mini.reloc"; then
    echo 'MINIAS_A64_LDR_BINDING=FAIL local_reference_not_folded' >&2
    exit 1
fi

for flavor in gnu mini; do
    "$AARCH64_LD" -e ldr_binding_entry -Ttext=0x10000 -o "$work/$flavor.elf" \
        "$work/$flavor.o" "$work/strong.o"
    readelf -Ws "$work/$flavor.elf" | awk '
        $8=="weak_literal" {if($5!="GLOBAL")exit 1; found=1}
        END {if(!found)exit 1}
    '
    text_bytes "$work/$flavor.elf" "$work/$flavor.linked.text"
done
cmp "$work/gnu.linked.text" "$work/mini.linked.text" || {
    echo 'MINIAS_A64_LDR_BINDING=FAIL linked_strong_override' >&2
    exit 1
}
echo 'MINIAS_A64_LDR_BINDING=PASS GNU_bytes=exact LD_relocations=4 pool_ABS64=1 LOCAL_fold=1 weak_override=GNU_ld'
