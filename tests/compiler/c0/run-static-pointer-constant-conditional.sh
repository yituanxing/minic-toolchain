#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/static-pointer-constant-conditional
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_pointer_constant_conditional.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
grep -F 'selected:' "$work/output.s" >/dev/null
grep -F 'null_selected:' "$work/output.s" >/dev/null
grep -F '  .dword value' "$work/output.s" >/dev/null
grep -F '  .dword 0' "$work/output.s" >/dev/null
grep -F 'absolute_pointer_poison:' "$work/output.s" >/dev/null
grep -F 'absolute_pointer_bits:' "$work/output.s" >/dev/null
grep -F 'same_function_conditional:' "$work/output.s" >/dev/null
same_function_count=$(grep -F -c '  .dword static_pointer_function' "$work/output.s")
test "$same_function_count" -eq 1

if "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_nonconstant_conditional.c" \
    -o "$work/invalid-nonconstant-conditional.s" \
    >"$work/invalid-nonconstant-conditional.stdout" \
    2>"$work/invalid-nonconstant-conditional.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-pointer-constant-conditional: unequal nonconstant arms accepted' >&2
    exit 1
fi
grep -F 'static pointer initializer requires a null or static symbol address constant' \
    "$work/invalid-nonconstant-conditional.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/static-pointer-constant-conditional symbol+null selected-by-ICE absolute-bits=full-width absolute-pointer-arithmetic=target-width identical-arms=normalized unequal-arms=fail-closed'
