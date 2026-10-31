#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu=${CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-x86-immediate-qualification
mkdir -p "$work"

"$gnu" -std=gnu11 -fsyntax-only \
    "$root/tests/compiler/c0/x86_asm_lowercase_i_valid.c"
"$minic" --target=x86_64-linux -S \
    "$root/tests/compiler/c0/x86_asm_lowercase_i_valid.c" \
    -o "$work/lowercase.s"
"$gnu" -no-pie "$work/lowercase.s" -o "$work/lowercase"
"$work/lowercase"

for family in uppercase_I mixed_Ir; do
    source="$root/tests/compiler/c0/x86_asm_${family}_unqualified.c"
    "$gnu" -std=gnu11 -fsyntax-only "$source"
    if "$minic" --target=x86_64-linux -S "$source" -o "$work/$family.s" \
        >"$work/$family.stdout" 2>"$work/$family.stderr"; then
        printf '%s\n' "FAIL: unqualified x86 $family machine input admitted" >&2
        exit 1
    fi
    grep -F 'Core IR does not yet support' "$work/$family.stderr" >/dev/null
done

printf '%s\n' 'PASS x86-immediate-qualification lowercase-i=native-pass uppercase-I=reject mixed-Ir=reject GNU-oracle=3'
