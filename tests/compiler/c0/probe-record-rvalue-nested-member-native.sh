#!/usr/bin/env bash
# One temporary, one evaluation, actual GNU ELF/host execution for nested
# record-rvalue projection, plus four-ISA Core -> machine source admission.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
mini=${1:-${MINIC_CC:-"$root/build/release/bin/minic-cc"}}
target=${2:-x86_64}
fixture="$root/tests/compiler/c0/record_rvalue_nested_member.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf 'RECORD_RVALUE_NESTED=FAIL target=%s reason=%s\n' "$target" "$*" >&2; exit 1; }

test -x "$mini" || fail 'MiniC compiler absent'
cc -std=gnu11 -E -P -x c "$fixture" -o "$work/original.i"
"$mini" --target="$target-linux" -S "$work/original.i" -o "$work/mini.s" \
    || fail 'nested materialized record member rejected'
test -s "$work/mini.s" || fail 'MiniC returned success without assembly'

if [[ $target == x86_64 ]]; then
    cc -std=gnu11 -O0 "$fixture" -o "$work/gnu"
    cc -c -x assembler "$work/mini.s" -o "$work/mini.o"
    cc "$work/mini.o" -o "$work/mini"
    "$work/gnu" || fail 'GNU native baseline failed'
    "$work/mini" || fail 'MiniC native evaluation/copy/return differs from GNU'
    printf '%s\n' 'RECORD_RVALUE_NESTED_NATIVE=PASS GNU_runtime=1 MiniC_runtime=1 call_once=6'
fi
printf 'RECORD_RVALUE_NESTED_SOURCE=PASS target=%s chained_scalar=1 record_copy=2 record_return=2 conditional=1\n' "$target"
