#!/usr/bin/env bash
# Linux 6.6.143 arch/x86/kernel/alternative.c ALT_RELOC_SELFTEST:
# compare GCC's real memory-operand fixups against the typed MiniC backend.
# MC-062 uses two global 'm' operands to catch the old fixed-symbol shortcut.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
compiler=${1:-"$repo_root/build/bin/minic-cc"}
fixture="$repo_root/tests/compiler/audit/mc062_x86_alt_reloc_memory_operand.c"
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

check_relocations() {
    local object=$1 symbol=$2
    readelf -Wr "$object" | awk -v symbol="$symbol" '
        /^Relocation section / {
            alternative = ($0 ~ /\.rela\.altinstructions[^[:alnum:]_]/)
            replacement = ($0 ~ /\.rela\.altinstr_replacement[^[:alnum:]_]/)
            next
        }
        alternative && $3 ~ /^R_X86_64_/ { ++alt_count }
        replacement && $3 ~ /^R_X86_64_/ {
            ++replacement_count
            if ($3 == "R_X86_64_PC32" && $5 == symbol &&
                $6 == "-" && $7 == "4") ++memory_count
            if ($3 == "R_X86_64_PLT32" &&
                $5 == "__alt_reloc_selftest" &&
                $6 == "-" && $7 == "4") ++call_count
        }
        END {
            if (alt_count != 2 || replacement_count != 2 ||
                memory_count != 1 || call_count != 1) {
                printf "unexpected ALT relocations: alt=%d repl=%d mem=%d call=%d\n",
                    alt_count, replacement_count, memory_count, call_count > "/dev/stderr"
                exit 1
            }
        }
    '
}

for variant in other official; do
    declare_args=()
    symbol=other_alt_reloc_addr
    if [[ "$variant" == official ]]; then
        declare_args=(-DALT_GOOD)
        symbol=__alt_reloc_selftest_addr
    fi

    gcc -std=gnu11 -fno-pie "${declare_args[@]}" -c "$fixture" \
        -o "$tmpdir/$variant.gcc.o"
    gcc -E -P -x c "${declare_args[@]}" "$fixture" \
        -o "$tmpdir/$variant.i"
    "$compiler" -S "$tmpdir/$variant.i" --target=x86_64-linux -fno-pie \
        -o "$tmpdir/$variant.minic.s"
    gcc -x assembler -c "$tmpdir/$variant.minic.s" \
        -o "$tmpdir/$variant.minic.o"

    for owner in gcc minic; do
        object="$tmpdir/$variant.$owner.o"
        readelf -h "$object" | grep -Eq 'Type:[[:space:]]*REL \(Relocatable file\)'
        readelf -h "$object" | grep -Eq 'Machine:[[:space:]]*Advanced Micro Devices X86-64'
        check_relocations "$object" "$symbol"
        objcopy --dump-section ".altinstr_replacement=$tmpdir/$variant.$owner.replacement" "$object"
    done
    cmp "$tmpdir/$variant.gcc.replacement" "$tmpdir/$variant.minic.replacement"
done

printf 'MINIC_X86_ALT_RELOC_SELFTEST=PASS GNU_C=2 MINIC_ASM=2 GNU_AS=2 ELF_REL=4 altinstructions=2x2 replacement=2x2\n'
