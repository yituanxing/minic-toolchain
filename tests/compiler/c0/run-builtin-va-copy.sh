#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0/builtin-va-copy

rm -rf "$work"
mkdir -p "$work"

if ! command -v "$riscv_cc" >/dev/null 2>&1 || ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' 'SKIP compiler/c0/builtin_va_copy: RISC-V runtime tools unavailable'
    test "${REQUIRE_RISCV_RUNTIME:-0}" != 1
    exit
fi

compile_failure() {
    name=$1
    expected=$2

    "$riscv_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

name=builtin_va_copy
"$riscv_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
"$minic" -S "$work/$name.i" -o "$work/$name.s"
"$riscv_cc" -static "$work/$name.s" -o "$work/$name.elf"

set +e
"$qemu" "$work/$name.elf"
status=$?
set -e

if test "$status" -ne 0; then
    printf '%s\n' "FAIL compiler/c0/$name: expected=0 actual=$status" >&2
    exit 1
fi
printf '%s\n' "PASS compiler/c0/$name exit=$status semantic=target-aware-va-list-copy"

compile_failure invalid_builtin_va_copy_target \
    'GNU va builtin requires a modifiable va_list lvalue'
compile_failure invalid_builtin_va_copy_source \
    '__builtin_va_copy source must be a va_list lvalue'
