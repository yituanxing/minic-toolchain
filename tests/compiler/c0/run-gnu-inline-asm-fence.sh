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


# GNU emits no instructions for an operand-free nonvolatile asm("").
# It must lower without inventing an effect or rejecting a source TU.
cat >"$work/gnu_nonvolatile_empty.i" <<'SRC'
int empty_gnu_asm(unsigned int x) {
    __asm__("");
    return (int)(x + 2U);
}
SRC
"$minic" -S "$work/gnu_nonvolatile_empty.i" \
    -o "$work/gnu_nonvolatile_empty.s"
grep -F 'empty_gnu_asm:' "$work/gnu_nonvolatile_empty.s" >/dev/null
if grep -F '__asm__' "$work/gnu_nonvolatile_empty.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/gnu_inline_asm_fence: unexpected empty asm text' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/gnu_inline_asm_fence template=concatenated volatile=1 outputs=0 inputs=0 clobber=memory RV64=fence'
