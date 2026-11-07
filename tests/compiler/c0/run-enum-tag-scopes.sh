#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-enum-tag-scopes

mkdir -p "$work"

"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
    "$root/tests/compiler/c0/enum_tag_scopes.c"
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/enum_tag_scopes.c" -o "$work/valid.i"
"$minic" -S --target=x86_64-linux "$work/valid.i" -o "$work/valid.s"
test -s "$work/valid.s"
"$host_cc" -no-pie "$work/valid.s" -o "$work/valid"
"$work/valid"

if "$host_cc" -std=gnu11 -fsyntax-only \
    "$root/tests/compiler/c0/invalid_enum_tag_scope_leak.c" \
    >"$work/gcc-invalid.out" 2>"$work/gcc-invalid.err"; then
    printf '%s\n' 'FAIL enum-tag-scopes: GNU C accepted leaked block enum tag' >&2
    exit 1
fi
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/invalid_enum_tag_scope_leak.c" \
    -o "$work/invalid.i"
if "$minic" -S --target=x86_64-linux "$work/invalid.i" -o "$work/invalid.s" \
    >"$work/minic-invalid.out" 2>"$work/minic-invalid.err"; then
    printf '%s\n' 'FAIL enum-tag-scopes: MiniC accepted leaked block enum tag' >&2
    exit 1
fi
grep -F 'incomplete' "$work/minic-invalid.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/enum-tag-scopes gcc-oracle=1 nested-shadow=1 forward-shadow=1 scope-restore=1 out-of-scope=reject runtime=1'
