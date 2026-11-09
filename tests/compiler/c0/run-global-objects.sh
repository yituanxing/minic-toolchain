#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$root/tests/compiler/c0/machine-cfg-contract.sh"
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-global-objects

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
    global_array_read \
    "$root/tests/programs/c0/global_array_read.c"
grep -F ".section .rodata" "$work/global_array_read.s" >/dev/null
grep -F "table:" "$work/global_array_read.s" >/dev/null
require_machine_function_cfg "$work/global_array_read.s" read_table 2
# Core owns pointer stride semantically; the RV64 emitter need not preserve the
# legacy emitter's strength-reduced slli-by-two peephole for int indexing.
grep -E '^[[:space:]]+lla[[:space:]]+[^,]+,[[:space:]]*table$' \
    "$work/global_array_read.s" >/dev/null
# Four-byte int index scaling is strength-reduced to SLLI by the RV64I
# machine selector; requiring MUL would force an unsupported M extension.
grep -E '^[[:space:]]+slli[[:space:]]+[^,]+,[[:space:]]*[^,]+,[[:space:]]*2$' \
    "$work/global_array_read.s" >/dev/null
grep -E '^[[:space:]]+lw[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)$' \
    "$work/global_array_read.s" >/dev/null
grep -F "  call read_table" "$work/global_array_read.s" >/dev/null
test "$(grep -E -c '^[[:space:]]+lla[[:space:]]+[^,]+,[[:space:]]*table$' "$work/global_array_read.s")" -eq 1
printf '%s\n' "PASS compiler/c0/global_array_read normalized=core-global-address"

expect_failure \
    invalid_global_array_assignment \
    "assignment expression requires a modifiable object lvalue"
expect_failure \
    invalid_bare_global_array \
    "return expression does not match function return type"
