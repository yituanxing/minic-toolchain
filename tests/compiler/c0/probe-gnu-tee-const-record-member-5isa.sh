#!/bin/sh
# Linux tee_session_calc_client_uuid: record statement-expression member
# projection through const/volatile cred and subsequent variadic conversion.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
: "${MINIC_CC:?set MINIC_CC to a built minic-cc}"
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/gnu_tee_nested_const_record_member.c"
negative="$root/tests/compiler/c0/invalid_gnu_tee_rvalue_member_address.c"
"$host_cc" -E -P -std=gnu11 "$source" -o "$work/positive.i"
"$host_cc" -E -P -std=gnu11 "$negative" -o "$work/negative.i"

for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$MINIC_CC" --target="$target" -S "$work/positive.i" -o "$work/$target.s"
    test -s "$work/$target.s"
    if "$MINIC_CC" --target="$target" -S "$work/negative.i" -o "$work/invalid-$target.s" \
        >"$work/invalid-$target.out" 2>"$work/invalid-$target.err"; then
        echo "FAIL $target accepted address of record statement rvalue" >&2
        exit 1
    fi
done
if "$host_cc" -std=gnu11 -fsyntax-only "$negative" >"$work/gnu-negative.out" 2>"$work/gnu-negative.err"; then
    echo 'FAIL GNU accepted address of record statement rvalue' >&2
    exit 1
fi
"$host_cc" -O0 -no-pie -x c "$work/positive.i" -o "$work/gnu-native"
"$host_cc" -no-pie -x assembler "$work/x86_64-linux.s" -o "$work/minic-native"
"$work/gnu-native"
"$work/minic-native"
echo 'MINIC_GNU_TEE_CONST_RECORD_MEMBER=PASS five_isa=5 qualified_member_rvalue=const+volatile GNU_native_diff=1 negative_address=5'
