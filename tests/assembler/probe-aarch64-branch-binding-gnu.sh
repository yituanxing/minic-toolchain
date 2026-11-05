#!/usr/bin/env bash
# AArch64 branch identity is link-time state for all non-LOCAL symbols.
# The Linux sparse-vmemmap.o weak pmd_init/pud_init/vmemmap_* functions must
# resolve to architecture-specific strong definitions, even if their weak
# fallback has a definition in the same input .text section.
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

compare_text_and_relocs() {
    local label=$1 gnu=$2 mini=$3 expected=$4
    text_bytes "$gnu" "$work/$label.gnu.text"
    text_bytes "$mini" "$work/$label.mini.text"
    cmp "$work/$label.gnu.text" "$work/$label.mini.text" || {
        echo "MINIAS_AARCH64_BRANCH_BINDING=FAIL source=$label stage=text" >&2
        return 1
    }
    text_relocs "$gnu" >"$work/$label.gnu.reloc"
    text_relocs "$mini" >"$work/$label.mini.reloc"
    cmp "$work/$label.gnu.reloc" "$work/$label.mini.reloc" || {
        echo "MINIAS_AARCH64_BRANCH_BINDING=FAIL source=$label stage=relocs" >&2
        diff -u "$work/$label.gnu.reloc" "$work/$label.mini.reloc" >&2 || true
        return 1
    }
    [[ $(wc -l <"$work/$label.gnu.reloc") == "$expected" ]] || {
        echo "MINIAS_AARCH64_BRANCH_BINDING=FAIL source=$label stage=reloc-count" >&2
        return 1
    }
}

source="$here/fixtures/aarch64-branch-binding-gnu.S"
strong="$here/fixtures/aarch64-branch-binding-strong-gnu.S"
"$AARCH64_AS" -o "$work/gnu.o" "$source"
"$MINIAS" --target=aarch64 -o "$work/mini.o" "$source"
"$AARCH64_AS" -o "$work/strong.o" "$strong"
compare_text_and_relocs fixture "$work/gnu.o" "$work/mini.o" 8

# A strict linked-image A/B consumes all relocations with the same GNU ld;
# the strong weak_target body must replace the weak fallback in both images.
for flavor in gnu mini; do
    "$AARCH64_LD" -e branch_entry -Ttext=0x10000 -o "$work/$flavor.elf" \
        "$work/$flavor.o" "$work/strong.o"
    # In the final symbol table the ELF linker must have selected the
    # second object's GLOBAL definition, not this object's WEAK fallback.
    readelf -Ws "$work/$flavor.elf" | awk '
        $8=="weak_target" {if($5!="GLOBAL") exit 1; found=1}
        END {if(!found) exit 1}
    '
    text_bytes "$work/$flavor.elf" "$work/$flavor.linked.text"
done
cmp "$work/gnu.linked.text" "$work/mini.linked.text" || {
    echo 'MINIAS_AARCH64_BRANCH_BINDING=FAIL stage=strong-override-linked-text' >&2
    exit 1
}

# Optional exact Linux owner replay: same original GNU GCC .s, GNU AS and
# MiniAS, with symbol-specific call/jump relocation parity rather than a
# whole-object DWARF comparison that may differ in unrelated debug sections.
if [[ -n ${MINIAS_AARCH_SPARSE_SOURCE:-} ]]; then
    "$AARCH64_AS" -march=armv8.5-a -o "$work/sparse.gnu.o" \
        "$MINIAS_AARCH_SPARSE_SOURCE"
    "$MINIAS" --target=aarch64 -o "$work/sparse.mini.o" \
        "$MINIAS_AARCH_SPARSE_SOURCE"
    # The real frozen Linux object has more than 8 code relocations; both
    # sidecar objects must agree on exact tuple sequence and text bytes.
    text_bytes "$work/sparse.gnu.o" "$work/sparse.gnu.text"
    text_bytes "$work/sparse.mini.o" "$work/sparse.mini.text"
    cmp "$work/sparse.gnu.text" "$work/sparse.mini.text" || {
        echo 'MINIAS_AARCH64_BRANCH_BINDING=FAIL source=sparse stage=text' >&2
        exit 1
    }
    text_relocs "$work/sparse.gnu.o" >"$work/sparse.gnu.reloc"
    text_relocs "$work/sparse.mini.o" >"$work/sparse.mini.reloc"
    # AArch64 GNU may legitimately choose the .ref.text section instead of
    # the identical LOCAL function at offset zero; ignore only that known
    # alias and still compare every offset, type, addend and other symbol.
    sed 's/\.ref\.text + 0$/__earlyonly_bootmem_alloc + 0/' \
        "$work/sparse.gnu.reloc" >"$work/sparse.gnu.reloc.norm"
    cmp "$work/sparse.gnu.reloc.norm" "$work/sparse.mini.reloc" || {
        echo 'MINIAS_AARCH64_BRANCH_BINDING=FAIL source=sparse stage=relocs' >&2
        diff -u "$work/sparse.gnu.reloc.norm" "$work/sparse.mini.reloc" >&2 || true
        exit 1
    }
    for weak in pmd_init pud_init vmemmap_check_pmd vmemmap_set_pmd; do
        grep -q "R_AARCH64_CALL26 $weak + 0" "$work/sparse.mini.reloc" || {
            echo "MINIAS_AARCH64_BRANCH_BINDING=FAIL stage=missing-weak-call symbol=$weak" >&2
            exit 1
        }
    done
    echo 'MINIAS_AARCH64_BRANCH_BINDING_SPARSE=PASS text_exact=1 relocs_exact=1 weak_calls=4'
fi

echo 'MINIAS_AARCH64_BRANCH_BINDING=PASS text_exact=1 relocs=8 GNU_ld_strong_override=PASS'
