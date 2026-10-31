#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-incomplete-arrays

rm -rf "$work"
mkdir -p "$work"

for name in extern_incomplete_array extern_incomplete_array_only; do
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
done

grep -F '.globl message' "$work/extern_incomplete_array.s" >/dev/null
grep -F '.size message, 5' "$work/extern_incomplete_array.s" >/dev/null
grep -F '  .byte 97' "$work/extern_incomplete_array.s" >/dev/null
grep -F '  .byte 100' "$work/extern_incomplete_array.s" >/dev/null
# Address lowering may use a temporary then spill it rather than materialize
# directly into a0.  Either form still proves that the extern symbol survives
# the source/IR boundary and is referenced by its actual linker name.
grep -Eq '^[[:space:]]+(la|lla) [at][0-9]+, message$' "$work/extern_incomplete_array.s"
grep -Eq '^[[:space:]]+(la|lla) [at][0-9]+, remote_message$' "$work/extern_incomplete_array_only.s"
if grep -F '.globl remote_message' "$work/extern_incomplete_array_only.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/extern_incomplete_array_only: extern declaration emitted a definition' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/extern_incomplete_arrays declaration=1 completion=string-inferred use-before-definition=1'
