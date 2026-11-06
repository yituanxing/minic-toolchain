#!/bin/sh
# All eight privileged CRn GNU source forms, including tied and plain CR0/4,
# plus the optional fake-memory dependency. Compile and assemble, never run.
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM
"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_control_register.c" \
    -o "$workdir/control.i"
"$minic_cc" -S "$workdir/control.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/control.s"
"$host_cc" -c -x assembler "$workdir/control.s" -o "$workdir/control.o"
objdump -d "$workdir/control.o" > "$workdir/control.dis"
read_count=$(grep -Ec 'mov[[:space:]]+%cr[0-4],%rax' "$workdir/control.dis")
write_count=$(grep -Ec 'mov[[:space:]]+%rax,%cr[0-4]' "$workdir/control.dis")
if [ "$read_count" -ne 5 ] || [ "$write_count" -ne 6 ]; then
    printf 'FAIL: CRn hardware instructions read=%s write=%s (expected 5/6)\n' \
        "$read_count" "$write_count" >&2
    exit 1
fi
for register in cr0 cr2 cr3 cr4
do
    if ! grep -Fq "%$register,%rax" "$workdir/control.dis"; then
        printf 'FAIL: %s physical read absent\n' "$register" >&2
        exit 1
    fi
done
printf 'PASS: x86-64 CR0/2/3/4 typed MachineIR control state (11 physical ops)\n'
