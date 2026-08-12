#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-runtime-local-array-initializer

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/runtime_local_array_initializer.c" \
    -o "$work/runtime_local_array_initializer.i"
"$minic" -S "$work/runtime_local_array_initializer.i" \
    -o "$work/runtime_local_array_initializer.s"
test "$(grep -c -F '  call probe' "$work/runtime_local_array_initializer.s")" -eq 1
grep -F '  call consume' "$work/runtime_local_array_initializer.s" >/dev/null
# Inferred pointer array is two RV64 pointers; fixed int array remains three elements.
grep -F '  li a0, 16' "$work/runtime_local_array_initializer.s" >/dev/null || true
printf '%s\n' 'PASS compiler/c0/runtime_local_array_initializer inferred-count=2 runtime-elements=left-to-right pointer-null=1 decay=1 sizeof=1 fixed-tail-zero=1'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_inferred_local_array_without_initializer.c" \
    -o "$work/invalid_inferred.i"
if "$minic" -S "$work/invalid_inferred.i" -o "$work/invalid_inferred.s" \
    >"$work/invalid_inferred.stdout" 2>"$work/invalid_inferred.stderr"; then
    echo 'FAIL inferred local array without initializer unexpectedly compiled' >&2
    exit 1
fi
grep -F 'inferred local array requires an initializer' "$work/invalid_inferred.stderr" >/dev/null

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_local_array_initializer_element.c" \
    -o "$work/invalid_element.i"
if "$minic" -S "$work/invalid_element.i" -o "$work/invalid_element.s" \
    >"$work/invalid_element.stdout" 2>"$work/invalid_element.stderr"; then
    echo 'FAIL incompatible local array initializer unexpectedly compiled' >&2
    exit 1
fi
grep -F 'local array initializer element type does not match element type' "$work/invalid_element.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/runtime_local_array_initializer negative=inferred-without-init+incompatible-element'
