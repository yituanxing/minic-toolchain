#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-aarch64-va-list-declaration
fixture="$root/tests/compiler/c0/aarch64_builtin_va_list_declaration.c"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 "$fixture" -o "$work/accepted.i"
"$minic" --target=aarch64-linux -S "$work/accepted.i" -o "$work/accepted.s"
grep -F 'builtin_va_declaration_probe:' "$work/accepted.s" >/dev/null
grep -F 'ldp x11, x12, [x10]' "$work/accepted.s" >/dev/null
grep -F 'stp x13, x14, [x9, #16]' "$work/accepted.s" >/dev/null

for invalid in INVALID_CONST_DEST INVALID_INTEGER_DEST INVALID_INTEGER_SOURCE; do
    "$host_cc" -E -P -std=gnu11 "-D$invalid" "$fixture" -o "$work/$invalid.i"
    if "$minic" --target=aarch64-linux -S "$work/$invalid.i" \
        -o "$work/$invalid.s" >"$work/$invalid.out" \
        2>"$work/$invalid.err"; then
        printf '%s\n' "FAIL AArch64 va_list: $invalid was accepted" >&2
        exit 1
    fi
    grep -E 'GNU va builtin requires a modifiable va_list lvalue|__builtin_va_copy source must be a va_list lvalue' \
        "$work/$invalid.err" >/dev/null
done

printf '%s\n' 'PASS compiler/c0/aarch64_builtin_va_list_declaration direct_type=1 typedef_type=1 aapcs64_cursor_copy=2 invalid_cursor=3'
