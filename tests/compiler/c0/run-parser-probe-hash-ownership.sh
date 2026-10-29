#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/parser_probe_hash_ownership.c" -o "$work/positive.i"
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/invalid_parser_probe_hash_ownership.c" -o "$work/negative.i"
"$host_cc" -c -std=gnu11 "$root/tests/compiler/c0/parser_probe_hash_ownership.c" \
    -o "$work/positive-gnu.o"
if "$host_cc" -c -std=gnu11 \
    "$root/tests/compiler/c0/invalid_parser_probe_hash_ownership.c" \
    -o "$work/negative-gnu.o" 2>"$work/negative-gnu.err"; then
    printf '%s\n' 'FAIL GNU: nonconstant static array designator accepted' >&2
    exit 1
fi

for target in x86_64-linux riscv64-linux riscv32-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$work/positive.i" \
        -o "$work/positive-$target.s"
    if "$minic_cc" --target="$target" -S "$work/negative.i" \
        -o "$work/negative-$target.s" 2>"$work/negative-$target.err"; then
        printf '%s\n' "FAIL $target: nonconstant static array designator accepted" >&2
        exit 1
    fi
    if ! grep -F 'error:' "$work/negative-$target.err" >/dev/null; then
        printf '%s\n' "FAIL $target: nonconstant static array designator crashed" >&2
        exit 1
    fi
done

printf '%s\n' \
    'PASS parser probe hash ownership: GNU positive, five ISA positive/negative, no double-free'
