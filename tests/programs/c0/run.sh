#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-buildroot-linux-musl-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/programs-c0

if ! command -v "$riscv_cc" >/dev/null 2>&1 ||
   ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' "SKIP programs/c0: set RISCV_CC and QEMU_RISCV64"
    test "${REQUIRE_RISCV_RUNTIME:-0}" != 1
    exit
fi

mkdir -p "$work"

run_program() {
    name=$1
    source="$root/tests/programs/c0/$name.c"

    "$riscv_cc" -E -P -x c "$source" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.minic.s"
    "$riscv_cc" -static "$work/$name.minic.s" -o "$work/$name.minic.elf"
    "$riscv_cc" -std=c11 -O0 -static "$source" -o "$work/$name.gcc.elf"

    set +e
    "$qemu" "$work/$name.minic.elf"
    minic_status=$?
    "$qemu" "$work/$name.gcc.elf"
    gcc_status=$?
    set -e

    if test "$minic_status" -ne "$gcc_status"; then
        printf '%s\n' \
            "FAIL programs/c0/$name: MiniC=$minic_status GCC=$gcc_status" >&2
        exit 1
    fi
    printf '%s\n' "PASS programs/c0/$name exit=$minic_status"
}

run_program gcd
run_program fibonacci
run_program prime_count
run_program collatz
