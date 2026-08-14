#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-builtin-constant-p
assembly="$work/gnu_builtin_constant_p.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_builtin_constant_p.c" \
    -o "$work/gnu_builtin_constant_p.i"
"$minic" -S "$work/gnu_builtin_constant_p.i" -o "$assembly"

test -s "$assembly"
grep -F 'runtime_probe:' "$assembly" >/dev/null
grep -F 'folded_probe:' "$assembly" >/dev/null
if grep -F '__builtin_constant_p' "$assembly" >/dev/null; then
    printf '%s\n' '__builtin_constant_p leaked into emitted assembly' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/gnu_builtin_constant_p query=compile-time integer-ICE=1 nonconstant=0 runtime-evaluation=none'
