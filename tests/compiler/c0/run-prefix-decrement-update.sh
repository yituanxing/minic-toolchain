#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-prefix-decrement-update

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/prefix_decrement_update.c" \
    -o "$work/prefix_decrement_update.i"
"$minic" -S \
    "$work/prefix_decrement_update.i" \
    -o "$work/prefix_decrement_update.s"

# Core lowers --value through explicit integer subtraction rather than the old
# AST emitter's addi -1 peephole.  Keep the test on the semantic operation and
# Core CFG, not on a specific scratch-register optimization.
grep -F ".Lmain_core_bb" "$work/prefix_decrement_update.s" >/dev/null
grep -F ".Lmain_core_return:" "$work/prefix_decrement_update.s" >/dev/null
if ! grep -E '^[[:space:]]+sub[[:space:]]' "$work/prefix_decrement_update.s" >/dev/null; then
    printf '%s\n' "FAIL compiler/c0/prefix_decrement_update: missing Core integer subtraction" >&2
    cat "$work/prefix_decrement_update.s" >&2
    exit 1
fi
printf '%s\n' "PASS compiler/c0/prefix_decrement_update"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/for_pointer_decrement_update.c" \
    -o "$work/for_pointer_decrement_update.i"
"$minic" -S \
    "$work/for_pointer_decrement_update.i" \
    -o "$work/for_pointer_decrement_update.s"
printf '%s\n' "PASS compiler/c0/for_pointer_decrement_update"
