#!/bin/sh
# All four forms are target-qualified and independently physically sealed.
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM
"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_boot_flags.c" \
    -o "$workdir/flags.i"
"$minic_cc" -S "$workdir/flags.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/flags.s"
"$host_cc" -c -x assembler "$workdir/flags.s" -o "$workdir/flags.o"
objdump -d "$workdir/flags.o" > "$workdir/flags.dump"
for opcode in 'bt ' 'bts ' cpuid pushf popf
do
    if ! grep -Fq "$opcode" "$workdir/flags.dump"; then
        printf 'FAIL: original x86 boot flag instruction %s absent\n' "$opcode" >&2
        exit 1
    fi
done
printf 'PASS: Linux boot flag x86 four typed ASM forms + GNU AS\n'
