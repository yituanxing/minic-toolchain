#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
target_cc=${TARGET_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-prefetch

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/builtin_prefetch.c" -o "$work/builtin_prefetch.i"
"$minic" -S "$work/builtin_prefetch.i" -o "$work/builtin_prefetch.s"
if grep -F '__builtin_prefetch' "$work/builtin_prefetch.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/builtin_prefetch: runtime builtin symbol leaked' >&2
    exit 1
fi
"$target_cc" -static "$work/builtin_prefetch.s" -o "$work/builtin_prefetch"
"$qemu" "$work/builtin_prefetch"

check_invalid() {
    name=$1
    message=$2
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" >"$work/$name.out" 2>"$work/$name.err"; then
        printf 'FAIL compiler/c0/%s: compilation unexpectedly succeeded\n' "$name" >&2
        exit 1
    fi
    grep -F "$message" "$work/$name.err" >/dev/null
}

check_invalid invalid_builtin_prefetch_rw '__builtin_prefetch rw must be between 0 and 2'
check_invalid invalid_builtin_prefetch_locality '__builtin_prefetch locality must be between 0 and 3'
check_invalid invalid_builtin_prefetch_address '__builtin_prefetch address must have pointer type'

printf '%s\n' 'PASS compiler/c0/builtin_prefetch arity=1,2,3 address=pointer side-effects=preserved rw=0..2 locality=0..3 rv64-hint=optional'
