#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0

mkdir -p "$work"

compile_case() {
    name=$1
    expected=$2

    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
    cmp "$root/tests/compiler/c0/expected/$expected.s" "$work/$name.s"
    printf '%s\n' "PASS compiler/c0/$name"
}

compile_case empty_main return_0
compile_case return_0 return_0
compile_case return_42 return_42

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_return.c" -o "$work/invalid_return.i"
if "$minic" -S "$work/invalid_return.i" -o "$work/invalid_return.s" \
    >"$work/invalid_return.stdout" 2>"$work/invalid_return.stderr"; then
    printf '%s\n' "FAIL compiler/c0/invalid_return: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "expected decimal integer constant" "$work/invalid_return.stderr" >/dev/null
printf '%s\n' "PASS compiler/c0/invalid_return"
