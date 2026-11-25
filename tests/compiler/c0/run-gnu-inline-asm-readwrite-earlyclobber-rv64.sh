#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
rv_cc=${RV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-readwrite-earlyclobber
source="$root/tests/compiler/c0/gnu_inline_asm_readwrite_earlyclobber.c"
rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$source" -o "$work/probe.i"
"$minic" -S "$work/probe.i" -o "$work/minic.s"
grep -F 'add a0, a0,' "$work/minic.s" >/dev/null
grep -F 'add a1, a1,' "$work/minic.s" >/dev/null
cat > "$work/crt0.S" <<'START'
.text
.globl _start
_start:
    call main
    li a7, 93
    ecall
START
"$rv_cc" -ffreestanding -fno-stack-protector -fno-pie -c -std=gnu11 "$source" -o "$work/gcc.o"
"$rv_cc" -nostdlib -static -no-pie "$work/crt0.S" "$work/minic.s" -o "$work/minic.elf"
"$rv_cc" -nostdlib -static -no-pie "$work/crt0.S" "$work/gcc.o" -o "$work/gcc.elf"
set +e
"$qemu" "$work/gcc.elf"
gcc_status=$?
"$qemu" "$work/minic.elf"
minic_status=$?
set -e
test "$gcc_status" -eq 0
test "$minic_status" -eq "$gcc_status"
printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_readwrite_earlyclobber_rv64 constraint=+&r fixed=a0,a1 unmatched_nonoverlap=1 qemu=1'
