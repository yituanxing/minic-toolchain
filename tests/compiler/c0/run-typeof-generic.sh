#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-typeof-generic
assembly="$work/typeof_generic.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/typeof_generic.c" \
    -o "$work/typeof_generic.i"
"$minic" -S "$work/typeof_generic.i" -o "$assembly"

test -s "$assembly"
for symbol in generic_selected_value generic_default_value generic_controlling_is_unevaluated \
              typeof_expression_size typeof_type_name_size typeof_generic_pointer_size; do
    grep -F "$symbol:" "$assembly" >/dev/null
done
# Selected/default generic values must survive frontend selection lowering.
grep -F '  li a0, 7' "$assembly" >/dev/null
grep -F '  li a0, 9' "$assembly" >/dev/null
grep -F '  li a0, 11' "$assembly" >/dev/null
# The controlling expression of _Generic is unevaluated: no emitted call to this declaration.
if grep -F 'call generic_side_effect' "$assembly" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/typeof_generic controlling expression was emitted' >&2
    exit 1
fi
# RV64 unsigned long and pointers are both eight bytes.
size8=$(grep -c '  li a0, 8' "$assembly" || true)
test "$size8" -ge 3

printf '%s\n' 'PASS compiler/c0/typeof_generic typeof=expression,type-name generic=typed,default controlling=unevaluated linux-shape=1'
