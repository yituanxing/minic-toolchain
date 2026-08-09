#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-for-declaration-initializers

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/for_declaration_initializer.c" \
    -o "$work/for_declaration_initializer.i"
"$minic" -S "$work/for_declaration_initializer.i" \
    -o "$work/for_declaration_initializer.s"
printf '%s\n' 'PASS compiler/c0/for_declaration_initializer scope=condition,update,body redeclare-after-loop=1'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_for_declaration_scope.c" \
    -o "$work/invalid_for_declaration_scope.i"
if "$minic" -S "$work/invalid_for_declaration_scope.i" \
    -o "$work/invalid_for_declaration_scope.s" \
    >"$work/invalid_for_declaration_scope.stdout" \
    2>"$work/invalid_for_declaration_scope.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_for_declaration_scope: compilation unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'use of undeclared local' "$work/invalid_for_declaration_scope.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_for_declaration_scope'
