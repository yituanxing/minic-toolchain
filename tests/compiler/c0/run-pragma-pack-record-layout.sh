#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pragma-pack

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/pragma_pack_record_layout.c" -o "$work/pragma_pack.s"
test -s "$work/pragma_pack.s"
grep -F '.globl main' "$work/pragma_pack.s" >/dev/null

expect_failure() {
    name=$1
    message=$2
    if "$minic" -S "$root/tests/compiler/c0/$name.c" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$message" "$work/$name.stderr" >/dev/null || {
        cat "$work/$name.stderr" >&2
        exit 1
    }
}

expect_failure invalid_pragma_pack_alignment 'unsupported pragma pack alignment'
expect_failure invalid_unknown_pragma 'unsupported pragma directive'
expect_failure invalid_preprocessor_directive 'unsupported preprocessor directive'

printf '%s\n' 'PASS compiler/c0/pragma_pack_record_layout state=TU pack=1 reset=1 record-layout=DataLayout forward=definition-time unknown=reject'
