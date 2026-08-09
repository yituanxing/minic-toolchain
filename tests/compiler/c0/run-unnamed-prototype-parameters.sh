#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-unnamed-prototype-parameters

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/unnamed_prototype_parameter.c" \
    -o "$work/unnamed_prototype_parameter.i"
"$minic" -S \
    "$work/unnamed_prototype_parameter.i" \
    -o "$work/unnamed_prototype_parameter.s"
grep -F '  call install' "$work/unnamed_prototype_parameter.s" >/dev/null
grep -F '  call consume' "$work/unnamed_prototype_parameter.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/unnamed_prototype_parameter declaration=unnamed definition=named'

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_unnamed_definition_parameter.c" \
    -o "$work/invalid_unnamed_definition_parameter.i"
if "$minic" -S \
    "$work/invalid_unnamed_definition_parameter.i" \
    -o "$work/invalid_unnamed_definition_parameter.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_unnamed_definition_parameter: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'function definition requires parameter names' "$work/invalid.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_unnamed_definition_parameter'
