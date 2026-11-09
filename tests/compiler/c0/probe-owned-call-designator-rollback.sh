#!/usr/bin/env bash
# MC-009: real parser probe + 12-argument CALL; snapshot unit test checks ownership.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { echo "MC009_OWNED_CALL_ROLLBACK=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC binary missing'
"$cc" -std=gnu11 -E -P "$root/tests/compiler/audit/mc009_designator_sizeof_many_args.c" \
    -o "$work/case.i" || fail 'GNU preprocessing'
"$cc" -std=gnu11 -O0 -no-pie -x c "$work/case.i" -o "$work/gnu" ||
    fail 'GNU native build'
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/case.i" -o "$work/$target.s" ||
        fail "MiniC $target -S"
done
"$cc" -no-pie -x assembler "$work/x86_64-linux.s" -o "$work/minic" ||
    fail 'MiniC x86 native link'
gnu_status=0
minic_status=0
"$work/gnu" || gnu_status=$?
"$work/minic" || minic_status=$?
test "$gnu_status" -eq 7 || fail "GNU exit=$gnu_status, expected 7"
test "$minic_status" -eq "$gnu_status" ||
    fail "GNU=$gnu_status MiniC=$minic_status"
echo 'MC009_OWNED_CALL_ROLLBACK=PASS five_target_S_GNU_MiniC_native_7'
