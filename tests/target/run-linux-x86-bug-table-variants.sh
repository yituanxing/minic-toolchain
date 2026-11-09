#!/bin/sh
# Three original Linux BUG/WARN forms, including numeric no-discard records.
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM
"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_bug_table_variants.c" \
    -o "$workdir/variants.i"
"$minic_cc" -S "$workdir/variants.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/variants.s"
"$host_cc" -c -x assembler "$workdir/variants.s" \
    -o "$workdir/variants.o"
readelf -r "$workdir/variants.o" > "$workdir/relocations"
readelf -SW "$workdir/variants.o" > "$workdir/sections"
if ! grep -Fq '.rela__bug_table' "$workdir/relocations" ||
   ! grep -Fq '.rela.discard.reachable' "$workdir/relocations" ||
   ! grep -Fq 'R_X86_64_PC32' "$workdir/relocations" ||
   ! grep -Fq '__bug_table' "$workdir/sections"; then
    printf 'FAIL: Linux BUG table lost section or PC32 relocation\n' >&2
    exit 1
fi
records=$(awk '/Relocation section '\''\.rela__bug_table'\''/{in_bug=1;next} /Relocation section/{in_bug=0} in_bug && /R_X86_64_PC32/{n++} END{print n+0}' "$workdir/relocations")
if [ "$records" -ne 6 ]; then
    printf 'FAIL: BUG table expected 6 PC32 relocations, got %s\n' "$records" >&2
    exit 1
fi
printf 'PASS: Linux numeric/named BUG/WARN ELF metadata (3 variants)\n'
