#!/bin/sh
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM
input="$source_root/tests/target/x86_64_linux_three_call_alternative.c"
"$host_cc" -E -P "$input" -o "$workdir/alt.i"
"$minic_cc" -S "$workdir/alt.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/alt.s"
"$host_cc" -c -x assembler "$workdir/alt.s" -o "$workdir/alt.o"
readelf -SW "$workdir/alt.o" > "$workdir/sections"
readelf -r "$workdir/alt.o" > "$workdir/relocs"
for section in .altinstructions .altinstr_replacement .rela.altinstructions
do
    if ! grep -Fq "$section" "$workdir/sections"; then
        printf 'FAIL: missing three-CALL section %s\n' "$section" >&2
        exit 1
    fi
done
if [ "$(grep 'R_X86_64_PLT32' "$workdir/relocs" | grep -c 'clear_page_')" -ne 3 ]; then
    printf 'FAIL: three-CALL target relocs missing\n' >&2
    exit 1
fi
printf 'PASS: three CALL alternative sections and target relocations\n'
