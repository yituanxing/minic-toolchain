#!/usr/bin/env bash
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { printf '%s\n' "QUALIFIED_ARRAY_ADDRESS=FAIL $*" >&2; exit 1; }

"$host_cc" -std=gnu11 -E -P -x c \
    "$root/tests/compiler/c0/qualified_array_address_verifier.c" \
    -o "$work/positive.i" || fail 'GNU preprocessing'
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -O0 -no-pie -x c \
    "$work/positive.i" -o "$work/gnu" || fail 'GNU native compile'
"$work/gnu" || fail 'GNU native semantics'

for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/positive.i" -o "$work/$target.s" \
        || fail "$target C source-to-assembly"
    test -s "$work/$target.s" || fail "$target output missing"
done
"$host_cc" -no-pie -x assembler "$work/x86_64-linux.s" -o "$work/minic" \
    || fail 'MiniC GNU native assembler/linker'
"$work/minic" || fail 'MiniC native array address semantics'

for stem in qualified_array_address_const_reject \
            qualified_array_address_wrong_element_reject; do
    "$host_cc" -std=gnu11 -E -P -x c \
        "$root/tests/compiler/c0/$stem.c" -o "$work/$stem.i" \
        || fail "$stem GNU preprocessing"
    if "$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only -x c \
        "$work/$stem.i" >"$work/$stem.gnu.log" 2>&1; then
        fail "$stem GNU accepted invalid qualified array operation"
    fi
    if "$minic" --target=x86_64-linux -S "$work/$stem.i" \
        -o "$work/$stem.s" >"$work/$stem.minic.log" 2>&1; then
        fail "$stem MiniC accepted invalid qualified array operation"
    fi
    test ! -e "$work/$stem.s" || fail "$stem emitted partial assembly"
done

grep -F 'modifiable object lvalue' \
    "$work/qualified_array_address_const_reject.minic.log" >/dev/null \
    || fail 'const flexible array modification not rejected as const'
grep -F 'initializer type does not match' \
    "$work/qualified_array_address_wrong_element_reject.minic.log" >/dev/null \
    || fail 'incompatible pointer-to-array element types were accepted'

printf '%s\n' 'QUALIFIED_ARRAY_ADDRESS=PASS GNU_MiniC_x86_native=1 five_ISA_source_assembly=5/5 flexible_incomplete=1 zero_length=1 fixed=1 nested=1 const_volatile=1 negative=2/2'
