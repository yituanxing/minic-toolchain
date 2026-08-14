#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-asm-fence
assembly="$work/gnu_inline_asm_fence.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_inline_asm_fence.c" \
    -o "$work/gnu_inline_asm_fence.i"
"$minic" -S "$work/gnu_inline_asm_fence.i" -o "$assembly"

test -s "$assembly"
grep -F 'fence rw,w' "$assembly" >/dev/null
grep -F 'fence r,rw' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_fence template=concatenated volatile=1 outputs=0 inputs=0 clobber=memory RV64=fence'
