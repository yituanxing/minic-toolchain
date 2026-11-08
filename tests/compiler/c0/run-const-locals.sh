#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$root/tests/compiler/c0/machine-cfg-contract.sh"
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-const-locals

mkdir -p "$work"

compile_success() {
    name=$1
    source=$2

    "$host_cc" -E -P -x c "$source" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
}

expect_failure() {
    name=$1
    expected=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F "$expected" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/$name"
}

compile_success \
    const_local \
    "$root/tests/programs/c0/const_local.c"
grep -F ".type preserve_value, @function" "$work/const_local.s" >/dev/null
require_machine_function_cfg "$work/const_local.s" preserve_value 2
# Core emits XLEN arithmetic and then applies the semantic integer conversion;
# keep this gate on operations rather than the legacy emitter's W-form choice.
grep -E '^[[:space:]]+add[[:space:]]+' "$work/const_local.s" >/dev/null
# RV64I without M must lower this multiplication through __muldi3.
if ! grep -E '^[[:space:]]+mul[[:space:]]+' "$work/const_local.s" >/dev/null; then
    grep -E '^[[:space:]]+call[[:space:]]+__muldi3$' "$work/const_local.s" >/dev/null
fi
grep -F "  call preserve_value" "$work/const_local.s" >/dev/null
printf '%s\n' "PASS compiler/c0/const_local normalized=core-const-local"

expect_failure \
    invalid_const_local_assignment \
    "assignment expression requires a modifiable object lvalue"
expect_failure \
    invalid_const_local_increment \
    "prefix update requires a modifiable scalar lvalue"
