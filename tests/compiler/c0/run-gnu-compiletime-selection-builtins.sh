#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-compiletime-selection-builtins
assembly="$work/gnu_compiletime_selection_builtins.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_compiletime_selection_builtins.c" \
    -o "$work/gnu_compiletime_selection_builtins.i"
"$minic" -S "$work/gnu_compiletime_selection_builtins.i" -o "$assembly"

test -s "$assembly"
grep -F 'absolute_long:' "$assembly" >/dev/null
grep -F 'absolute_int:' "$assembly" >/dev/null
if grep -F '__builtin_choose_expr' "$assembly" >/dev/null ||
   grep -F '__builtin_types_compatible_p' "$assembly" >/dev/null; then
    printf '%s\n' 'compile-time builtin leaked into emitted assembly' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/gnu_compiletime_selection_builtins types-compatible=top-level-unqualified choose-expr=compile-time nested=1 statement-expression-arm=1'
