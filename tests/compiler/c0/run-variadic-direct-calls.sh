#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0/variadic-direct-calls

rm -rf "$work"
mkdir -p "$work"

if ! command -v "$riscv_cc" >/dev/null 2>&1 || ! command -v "$qemu" >/dev/null 2>&1; then
    printf '%s\n' \
        'SKIP compiler/c0/variadic_direct_call: set RISCV_CC and QEMU_RISCV64 to available tools'
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

name=variadic_direct_call
"$riscv_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
"$minic" -S "$work/$name.i" -o "$work/$name.s"
grep -F '  ld a5,' "$work/$name.s" >/dev/null
"$riscv_cc" -static \
    "$work/$name.s" \
    "$root/tests/compiler/c0/${name}_helper.c" \
    -o "$work/$name.elf"

set +e
"$qemu" "$work/$name.elf"
status=$?
set -e

if test "$status" -ne 0; then
    printf '%s\n' "FAIL compiler/c0/$name: expected=0 actual=$status" >&2
    exit 1
fi
printf '%s\n' \
    "PASS compiler/c0/$name exit=$status abi=rv64-varargs actual=6 fixed=1 extras=int,char,long,pointer,double"

compile_failure invalid_variadic_too_many_arguments 'variadic call supports at most 8 arguments'
compile_failure invalid_variadic_missing_fixed_argument \
    'call argument count does not match declaration'
compile_failure invalid_nonvariadic_extra_argument 'call argument count does not match declaration'
