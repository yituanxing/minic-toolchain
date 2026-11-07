#!/bin/sh
# Differential runtime test for Linux's three boot string ASM contracts.
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM
input="$source_root/tests/target/x86_64_linux_boot_string.c"
"$host_cc" -E -P "$input" -o "$workdir/boot_string.i"
"$minic_cc" -S "$workdir/boot_string.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/minic.s"
"$host_cc" -c -x assembler "$workdir/minic.s" -o "$workdir/minic.o"
"$host_cc" -no-pie "$workdir/minic.o" -o "$workdir/minic-test"
"$host_cc" -no-pie -O0 "$input" -o "$workdir/gcc-test"
"$workdir/gcc-test"
"$workdir/minic-test"
objdump -d "$workdir/minic.o" > "$workdir/instructions.dump"
for opcode in cmpsb divl movsq movsb
do
    if ! grep -q "$opcode" "$workdir/instructions.dump"; then
        printf 'FAIL: boot string hardware %s absent\n' "$opcode" >&2
        exit 1
    fi
done
printf 'PASS: Linux boot string complete triple ABI (MiniC/GNU AS vs GCC)\n'
