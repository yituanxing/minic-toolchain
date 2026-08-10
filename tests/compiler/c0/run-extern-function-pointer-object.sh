#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-function-pointer-object
assembly="$work/extern_function_pointer_object.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/extern_function_pointer_object.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F 'panic_blink_like' "$assembly" >/dev/null
if grep -F 'panic_blink_like:' "$assembly" >/dev/null ||
   grep -F '.globl panic_blink_like' "$assembly" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/extern_function_pointer_object: extern pointer object emitted storage' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/extern_function_pointer_object declarator=(*name)(params) function-type=owned pointer-object=extern storage=none'
