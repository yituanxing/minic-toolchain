#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-conditional-pointer-qualifiers

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/conditional_pointer_qualifiers.c" \
    -o "$work/conditional_pointer_qualifiers.i"
"$minic" -S "$work/conditional_pointer_qualifiers.i" \
    -o "$work/conditional_pointer_qualifiers.s"

grep -F 'choose_const:' "$work/conditional_pointer_qualifiers.s" >/dev/null
grep -F 'choose_volatile:' "$work/conditional_pointer_qualifiers.s" >/dev/null
grep -F 'choose_const_volatile:' "$work/conditional_pointer_qualifiers.s" >/dev/null
grep -F 'choose_const_void:' "$work/conditional_pointer_qualifiers.s" >/dev/null
grep -F 'add_intermediate_const:' "$work/conditional_pointer_qualifiers.s" >/dev/null

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_deep_pointer_qualification.c" \
    -o "$work/invalid_deep_pointer_qualification.i"
if "$minic" -S "$work/invalid_deep_pointer_qualification.i" \
    -o "$work/invalid_deep_pointer_qualification.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
    echo 'expected deep pointer qualification conversion to fail' >&2
    exit 1
fi
grep -F 'assignment expression type does not match target type' "$work/invalid.err" >/dev/null
printf '%s\n' 'PASS compiler/c0/conditional_pointer_qualifiers const=union volatile=union const-volatile=union void-object=qualified-void immediate-pointee-cv=add deeper-pointee-cv=reject'
