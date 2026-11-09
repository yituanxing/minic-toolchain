#!/bin/sh
# X87 discovery and privileged IRQ forms are compile-only, never executed.
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM
"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_fpu_boot.c" \
    -o "$workdir/fpu.i"
"$minic_cc" -S "$workdir/fpu.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/fpu.s"
"$host_cc" -c -x assembler "$workdir/fpu.s" -o "$workdir/fpu.o"
objdump -d "$workdir/fpu.o" > "$workdir/fpu.dump"
for opcode in fninit fnstsw fnstcw cli sti hlt
do
    if ! grep -Fq "$opcode" "$workdir/fpu.dump"; then
        printf 'FAIL: x86 FPU/IRQ opcode %s missing\n' "$opcode" >&2
        exit 1
    fi
done
printf 'PASS: Linux x87 status/control probe + privileged IRQ forms\n'
