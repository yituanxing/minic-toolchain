#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-parenthesized-functions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/extern_parenthesized_functions.c" \
    -o "$work/extern_parenthesized_functions.i"
"$minic" -S "$work/extern_parenthesized_functions.i" \
    -o "$work/extern_parenthesized_functions.s"

grep -F '  call external_add' "$work/extern_parenthesized_functions.s" >/dev/null
grep -F '  call external_alloc' "$work/extern_parenthesized_functions.s" >/dev/null
if grep -F '.type external_add, @function' "$work/extern_parenthesized_functions.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/extern_parenthesized_functions: declaration emitted a definition' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/extern_parenthesized_functions linkage=external parenthesized-name=1'
