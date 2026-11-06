#!/bin/sh
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM
input="$source_root/tests/target/x86_64_linux_paravirt_indirect_call.c"
"$host_cc" -E -P "$input" -o "$workdir/pv.i"
"$minic_cc" -S "$workdir/pv.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/pv.s"
"$host_cc" -c -x assembler "$workdir/pv.s" -o "$workdir/pv.o"
readelf -SW "$workdir/pv.o" > "$workdir/sections"
readelf -Wr "$workdir/pv.o" > "$workdir/relocs"
for section in .parainstructions .discard.retpoline_safe \
    .rela.parainstructions .rela.discard.retpoline_safe
do
    if ! grep -Fq "$section" "$workdir/sections"; then
        printf 'FAIL: missing paravirt ABI section %s\n' "$section" >&2
        exit 1
    fi
done
if [ "$(grep -c 'R_X86_64_64.*\.text' "$workdir/relocs")" -ne 1 ] || \
   [ "$(grep -c 'R_X86_64_32.*\.text' "$workdir/relocs")" -ne 1 ] || \
   [ "$(grep -c 'R_X86_64_PC32.*pv_ops' "$workdir/relocs")" -ne 1 ]; then
    printf 'FAIL: paravirt metadata or function-pointer relocation missing\n' >&2
    exit 1
fi
if [ "$(grep -Ec '^[[:space:]]*call \*\(%r10\)$' "$workdir/pv.s")" -ne 1 ]; then
    printf 'FAIL: paravirt call must use admitted memory-indirect encoding\n' >&2
    exit 1
fi
printf 'PASS: paravirt memory-indirect CALL, section metadata and relocations\n'
