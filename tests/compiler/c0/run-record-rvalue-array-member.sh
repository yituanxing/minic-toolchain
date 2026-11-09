#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-rvalue-array-member

mkdir -p "$work"

"$host_cc" -std=gnu11 -E -P -x c \
    "$root/tests/compiler/c0/record_rvalue_array_member.c" -o "$work/input.i"
MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -S \
    "$work/input.i" -o "$work/input.s"
"$minic_cc" --target=aarch64-linux -S "$work/input.i" -o "$work/aarch64.s"
"$minic_cc" --target=loongarch64-linux -S "$work/input.i" -o "$work/loongarch64.s"
"$host_cc" -no-pie "$work/input.s" -o "$work/input"
"$work/input"

"$host_cc" -std=gnu11 -E -P -x c \
    "$root/tests/compiler/c0/invalid_address_record_rvalue_array.c" \
    -o "$work/invalid.i"
if "$minic_cc" --target=x86_64-linux -S "$work/invalid.i" -o "$work/invalid.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo 'FAIL address of record rvalue array unexpectedly compiled' >&2
    exit 1
fi
grep -F 'address-of requires an lvalue object or function designator' \
    "$work/invalid.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/record_rvalue_array_member targets=x86_64,aarch64,loongarch64 decay=1 subscript=1 nested=1 conditional=1 sizeof=1 once-only-effects=1 nonlvalue=1 strict-core=1'
