#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-prefix-object-attributes
assembly="$work/static_prefix_object_attributes.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_prefix_object_attributes.c" \
    -o "$work/static_prefix_object_attributes.i"
"$minic" -S "$work/static_prefix_object_attributes.i" -o "$assembly"

test -s "$assembly"
grep -F 'class_irq_is_conditional:' "$assembly" >/dev/null
grep -F '  .byte 0' "$assembly" >/dev/null
grep -F '.size class_irq_is_conditional, 1' "$assembly" >/dev/null
grep -F 'class_irq_add:' "$assembly" >/dev/null
if grep -q 'static_declaration_is_function' src/frontend/parser_function.c; then
    printf '%s\n' 'static semantic declaration probe still present' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/static_prefix_object_attributes prefix=unused object=static-const-bool initializer=shared-ice-enum-false width=1-byte function=static-inline single-pass-head=1 semantic-probe=none'
