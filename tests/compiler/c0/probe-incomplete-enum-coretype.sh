#!/usr/bin/env bash
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fail() {
    printf '%s\n' "INCOMPLETE_ENUM_CORETYPE=FAIL $*" >&2
    exit 1
}

test -x "$minic" || fail 'MiniC compiler missing'
positive="$root/tests/compiler/c0/incomplete_enum_transitive_coretype.c"
negative="$root/tests/compiler/c0/incomplete_enum_byvalue_reject.c"
negative_call="$root/tests/compiler/c0/incomplete_enum_call_reject.c"

# GCC permits nominal, layout-free forward enum identities in declarations
# of function-pointer signatures, even across a complete record's members.
"$cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$positive" ||
    fail 'GNU rejected legal forward-enum callback declaration'
"$cc" -std=gnu11 -E -P -x c "$positive" -o "$work/positive.i" ||
    fail 'GNU positive preprocessing'
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/positive.i" -o "$work/$target.s" ||
        fail "$target C->Core->ASM"
    test -s "$work/$target.s" || fail "$target empty ASM"
done

"$cc" -std=gnu11 -no-pie -x c "$work/positive.i" -o "$work/gnu" ||
    fail 'GNU native link'
"$cc" -no-pie -x assembler "$work/x86_64-linux.s" -o "$work/minic" ||
    fail 'MiniC + GNU AS/LD native link'
"$work/gnu" || fail 'GNU complete-enum ABI oracle'
"$work/minic" || fail 'MiniC complete-enum ABI oracle'

# An incomplete enum carries no invented integer storage or calling
# convention. Materializing one by value must fail in both languages.
"$cc" -std=gnu11 -E -P -x c "$negative" -o "$work/negative.i" ||
    fail 'GNU negative preprocessing'
if "$cc" -std=gnu11 -fsyntax-only -x c "$work/negative.i" >"$work/gnu-negative.log" 2>&1; then
    fail 'GNU unexpectedly materialized an incomplete enum object'
fi
if "$minic" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.s" >"$work/minic-negative.log" 2>&1; then
    fail 'MiniC unexpectedly materialized an incomplete enum object'
fi
grep -F 'requires a complete object type' "$work/minic-negative.log" >/dev/null ||
    fail 'MiniC did not reject incomplete enum by-value object'

"$cc" -std=gnu11 -E -P -x c "$negative_call" -o "$work/negative-call.i" ||
    fail 'GNU incomplete-enum call preprocessing'
if "$cc" -std=gnu11 -fsyntax-only -x c "$work/negative-call.i" >"$work/gnu-negative-call.log" 2>&1; then
    fail 'GNU unexpectedly allowed an incomplete-enum callback result'
fi
if "$minic" --target=x86_64-linux -S "$work/negative-call.i" -o "$work/negative-call.s" >"$work/minic-negative-call.log" 2>&1; then
    fail 'MiniC invented an ABI for an incomplete-enum callback result'
fi
test ! -s "$work/negative-call.s" || fail 'MiniC wrote partial ASM for rejected call'

printf '%s\n' 'INCOMPLETE_ENUM_CORETYPE=PASS 5ISA_S=5/5 GNU_MiniC_native=1 complete_enum_abi=1 two_nominal_incomplete_enums=1 byvalue_object_reject=1 byvalue_call_reject=1'
