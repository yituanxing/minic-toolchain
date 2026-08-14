#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-file-scope-basic-asm
host_cc=${HOST_CC:-cc}

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/file_scope_basic_asm.c" -o "$work/file_scope_basic_asm.s"
test -s "$work/file_scope_basic_asm.s"
grep -F '.section ".export_symbol","a" ; __export_symbol_file_asm_target:' "$work/file_scope_basic_asm.s" >/dev/null
grep -F '.ascii "" "\0"' "$work/file_scope_basic_asm.s" >/dev/null
grep -F '.quad file_asm_target' "$work/file_scope_basic_asm.s" >/dev/null
grep -F '.ascii "%"' "$work/file_scope_basic_asm.s" >/dev/null
first=$(grep -n -F '__export_symbol_file_asm_target:' "$work/file_scope_basic_asm.s" | head -n1 | cut -d: -f1)
second=$(grep -n -F '__minic_file_asm_second:' "$work/file_scope_basic_asm.s" | head -n1 | cut -d: -f1)
third=$(grep -n -F '__minic_file_asm_third:' "$work/file_scope_basic_asm.s" | head -n1 | cut -d: -f1)
test "$first" -lt "$second"
test "$second" -lt "$third"
"$host_cc" -c "$work/file_scope_basic_asm.s" -o "$work/file_scope_basic_asm.o"

if "$minic" -S "$root/tests/compiler/c0/invalid_file_scope_basic_asm_qualifier.c" \
    -o "$work/invalid-qualifier.s" >"$work/invalid-qualifier.stdout" 2>"$work/invalid-qualifier.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/file-scope-basic-asm: qualifier accepted at file scope' >&2
    exit 1
fi
grep -F 'file-scope GNU basic asm does not allow qualifiers' "$work/invalid-qualifier.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_file_scope_basic_asm_operands.c" \
    -o "$work/invalid-operands.s" >"$work/invalid-operands.stdout" 2>"$work/invalid-operands.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/file-scope-basic-asm: extended operands accepted in basic-asm v0' >&2
    exit 1
fi
grep -F 'file-scope GNU basic asm does not support operands' "$work/invalid-operands.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/file-scope-basic-asm entity=translation-unit raw=verbatim aliases=asm+__asm+__asm__ strings=shared order=stable qualifiers+operands=fail-closed'
