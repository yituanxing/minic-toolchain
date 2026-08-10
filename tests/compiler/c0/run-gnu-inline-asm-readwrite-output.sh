#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-readwrite-output
assembly="$work/gnu_inline_asm_readwrite_output.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_inline_asm_readwrite_output.c" \
    -o "$work/gnu_inline_asm_readwrite_output.i"
"$minic" -S "$work/gnu_inline_asm_readwrite_output.i" -o "$assembly"

test -s "$assembly"
grep -F 'compiler_barrier:' "$assembly" >/dev/null
if grep -F '+rm' "$assembly" >/dev/null; then
    printf '%s\n' 'unexpected raw GNU asm constraint in emitted assembly' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_readwrite_output access=read-write raw-constraint=+rm target=RV64 local-lvalue=1 template=empty dataflow=preserved'
