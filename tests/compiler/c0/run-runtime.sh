#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-buildroot-linux-musl-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-runtime

if ! command -v "$riscv_cc" >/dev/null 2>&1 || ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' "SKIP compiler/c0/runtime: set RISCV_CC and QEMU_RISCV64 to available tools"
    test "${REQUIRE_RISCV_RUNTIME:-0}" != 1
    exit
fi

mkdir -p "$work"
"$riscv_cc" --version | sed -n '1p'
"$qemu" --version | sed -n '1p'

run_case() {
    name=$1
    expected=$2

    "$riscv_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
    "$riscv_cc" -static "$work/$name.s" -o "$work/$name.elf"

    set +e
    "$qemu" "$work/$name.elf"
    status=$?
    set -e

    if test "$status" -ne "$expected"; then
        printf '%s\n' "FAIL compiler/c0/runtime/$name: expected=$expected actual=$status" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/runtime/$name exit=$status"
}

run_case empty_main 0
run_case return_0 0
run_case return_42 42
