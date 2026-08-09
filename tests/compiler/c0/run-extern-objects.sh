#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-objects

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/extern_object.c" \
    -o "$work/extern_object.i"
"$minic" -S "$work/extern_object.i" -o "$work/extern_object.s"

grep -F '  la a0, external_counter' "$work/extern_object.s" >/dev/null
grep -F '  lw a0, 0(a0)' "$work/extern_object.s" >/dev/null
if grep -F '.type external_counter, @object' "$work/extern_object.s" >/dev/null ||
   grep -F 'external_counter:' "$work/extern_object.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/extern_object: declaration emitted a definition' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/extern_object declaration-only=external-symbol-reference'
