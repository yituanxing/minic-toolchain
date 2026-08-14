#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-multi-declarators
assembly="$work/extern_multi_declarators.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/extern_multi_declarators.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F '  la a0, start_like' "$assembly" >/dev/null
grep -F '  la a0, end_like' "$assembly" >/dev/null
if grep -F 'start_like:' "$assembly" >/dev/null || grep -F 'end_like:' "$assembly" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/extern_multi_declarators: declaration emitted storage' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/extern_multi_declarators shared-base=function-pointer arrays=2 incomplete=2 storage=none'
