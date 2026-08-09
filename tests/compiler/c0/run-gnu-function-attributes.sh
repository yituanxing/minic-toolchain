#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-function-attributes

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_attributes.c" \
    -o "$work/gnu_function_attributes.i"
"$minic" -S "$work/gnu_function_attributes.i" \
    -o "$work/gnu_function_attributes.s"

test -s "$work/gnu_function_attributes.s"
grep -F 'call_attribute_functions:' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call allocate_like' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call memory_copy' "$work/gnu_function_attributes.s" >/dev/null
grep -F '  call memory_compare' "$work/gnu_function_attributes.s" >/dev/null

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_attribute_reject.c" \
    -o "$work/gnu_function_attribute_reject.i"
set +e
"$minic" -S "$work/gnu_function_attribute_reject.i" \
    -o "$work/gnu_function_attribute_reject.s" \
    >"$work/reject.stdout" 2>"$work/reject.stderr"
status=$?
set -e
test "$status" -ne 0
grep -F 'unsupported GNU function attribute' "$work/reject.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_function_attributes metadata=nothrow,leaf,nonnull,access,pure,malloc,noreturn,deprecated placement=pre-declarator,suffix unknown=reject aligned=not-silently-ignored'
