#!/bin/sh
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM
"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_task_vdso.c" \
    -o "$workdir/task.i"
"$minic_cc" -S "$workdir/task.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/task.s"
"$host_cc" -c -x assembler "$workdir/task.s" -o "$workdir/task.o"
objdump -d "$workdir/task.o" > "$workdir/task.dis"
readelf -r "$workdir/task.o" > "$workdir/relocs"
readelf -SW "$workdir/task.o" > "$workdir/sections"
objdump -s -j .altinstr_replacement "$workdir/task.o" \
    > "$workdir/replacement"
for mnemonic in ltr str lsl
do
    if ! grep -Eq "\b$mnemonic\b" "$workdir/task.dis"; then
        printf 'FAIL: missing privileged/selector instruction %s\n' "$mnemonic" >&2
        exit 1
    fi
done
if [ "$(grep -c 'R_X86_64_PC32' "$workdir/relocs")" -ne 2 ]; then
    printf 'FAIL: vDSO alternatives did not produce two PC32 relocations\n' >&2
    exit 1
fi
for section in .altinstructions .altinstr_replacement .rela.altinstructions
do
    if ! grep -Fq "$section" "$workdir/sections"; then
        printf 'FAIL: missing vDSO patch section %s\n' "$section" >&2
        exit 1
    fi
done
if ! grep -Eq 'f30fc7f8' "$workdir/replacement"; then
    printf 'FAIL: exact 4-byte RDPID alternative is missing\n' >&2
    exit 1
fi
sed 's/(16\*32+22)/(16*32+23)/' "$workdir/task.i" > "$workdir/bad.i"
if "$minic_cc" -S "$workdir/bad.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/bad.s" \
    > "$workdir/bad.log" 2>&1; then
    printf 'FAIL: forged vDSO CPU feature was accepted\n' >&2
    exit 1
fi
printf 'PASS: x86 task LTR/STR and vDSO RDPID patch table (2 relocations)\n'
