#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/static-union-zero-overlay
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_zero_overlay.c" -o "$work/valid.i"
"$minic" -S "$work/valid.i" -o "$work/valid.s"
test -s "$work/valid.s"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_nonzero_overlay_invalid.c" -o "$work/nonzero.i"
"$minic" -S "$work/nonzero.i" -o "$work/nonzero.s"
test -s "$work/nonzero.s"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_active_member_relocation.c" -o "$work/active-relocation.i"
"$minic" -S "$work/active-relocation.i" -o "$work/active-relocation.s"
test -s "$work/active-relocation.s"
grep -Fq 'target' "$work/active-relocation.s"
echo 'PASS compiler/c0/static-union-active-member zero+nonzero=accepted relocation=layout-aware'
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/static_union_shape_overlay.c" -o "$work/shape-overlay.i"
"$minic" -S "$work/shape-overlay.i" -o "$work/shape-overlay.s"
test -s "$work/shape-overlay.s"
grep -Fq '.dword target' "$work/shape-overlay.s"
echo 'PASS compiler/c0/static-union-shape-overlay span-preserved relocation-after-union=correct'
