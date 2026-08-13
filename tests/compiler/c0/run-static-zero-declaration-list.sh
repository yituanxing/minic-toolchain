#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-static-zero-declaration-list"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/static_zero_declaration_list.c" -o "$work/positive.s"
for symbol in panic_later panic_param deep shallow first_pair second_pair; do
    grep -F "$symbol:" "$work/positive.s" >/dev/null
    if grep -F ".globl $symbol" "$work/positive.s" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/static-zero-declaration-list: exported $symbol" >&2
        exit 1
    fi
done

if "$minic" -S "$root/tests/compiler/c0/invalid_static_zero_declaration_list_initializer.c" \
    -o "$work/invalid.s" >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-zero-declaration-list-initializer: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'static zero-definition declaration list currently supports declarations only' \
    "$work/invalid.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/static-zero-declaration-list linkage=internal base-type=reparsed scalar=integer+pointer+record initializer=fail-closed'
