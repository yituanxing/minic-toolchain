#!/bin/sh
# All 12 debug-register forms, DR7 sentinel, and 3 head64 selector writes.
set -eu
minic_cc=${MINIC_CC:-build/debug/bin/minic-cc}
host_cc=${HOST_CC:-cc}
source_root=${MINIC_SOURCE_ROOT:-.}
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT HUP INT TERM
"$host_cc" -E -P "$source_root/tests/target/x86_64_linux_debug_boot.c" \
    -o "$workdir/debug-boot.i"
"$minic_cc" -S "$workdir/debug-boot.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/debug-boot.s"
"$host_cc" -c -x assembler "$workdir/debug-boot.s" -o "$workdir/debug-boot.o"
objdump -d "$workdir/debug-boot.o" > "$workdir/debug-boot.dis"
for register in db0 db1 db2 db3 db6 db7
do
    if ! grep -Eq "mov[[:space:]]+%$register,%rax" "$workdir/debug-boot.dis" ||
       ! grep -Eq "mov[[:space:]]+%rax,%$register" "$workdir/debug-boot.dis"; then
        printf 'FAIL: missing read/write of %s\n' "$register" >&2
        exit 1
    fi
done
for segment in ds ss es
do
    if ! grep -Eq "mov[[:space:]]+%eax,%$segment" "$workdir/debug-boot.dis"; then
        printf 'FAIL: missing GDT reload of %s\n' "$segment" >&2
        exit 1
    fi
done
if grep -Eq '\*0x1000|0x1000\(' "$workdir/debug-boot.dis"; then
    printf 'FAIL: DR7 sentinel was dereferenced\n' >&2
    exit 1
fi
sed 's/"mov %%db" "0"/"mov %%db" "4"/' "$workdir/debug-boot.i" \
    > "$workdir/unknown-dr.i"
if "$minic_cc" -S "$workdir/unknown-dr.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/unknown-dr.s" \
    > "$workdir/unknown-dr.log" 2>&1; then
    printf 'FAIL: unrecognized DR4 source template was admitted\n' >&2
    exit 1
fi
sed 's/movl %%eax, %%ss/movl %%eax, %%cs/' "$workdir/debug-boot.i" \
    > "$workdir/unknown-segment.i"
if "$minic_cc" -S "$workdir/unknown-segment.i" --target=x86_64-linux \
    -fshort-wchar -funsigned-char -fno-PIE -o "$workdir/unknown-segment.s" \
    > "$workdir/unknown-segment.log" 2>&1; then
    printf 'FAIL: unrecognized CS segment write was admitted\n' >&2
    exit 1
fi
printf 'PASS: x86 Linux 12 debug-register forms and 3 segment writes\n'
