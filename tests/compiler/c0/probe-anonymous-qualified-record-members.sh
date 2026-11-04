#!/usr/bin/env bash
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail() { echo "ANONYMOUS_QUALIFIED_RECORD=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC binary missing'
"$cc" -std=gnu11 -E -P "$root/tests/compiler/c0/anonymous_qualified_members.c" \
    -o "$work/positive.i" || fail 'GNU preprocessing'
"$cc" -std=gnu11 -O0 -no-pie -x c "$work/positive.i" \
    -o "$work/gnu" >"$work/gnu.log" 2>&1 || fail 'GNU native compilation'
"$work/gnu" || fail 'GNU native result'

for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/positive.i" \
        -o "$work/$target.s" || fail "$target source-to-assembly"
    test -s "$work/$target.s" || fail "$target missing assembly"
done
"$cc" -no-pie -x assembler "$work/x86_64-linux.s" -o "$work/minic" \
    || fail 'MiniC x86 native link'
"$work/minic" || fail 'MiniC x86 native result'

"$cc" -std=gnu11 -E -P \
    "$root/tests/compiler/c0/anonymous_qualified_member_const_reject.c" \
    -o "$work/negative.i" || fail 'GNU negative preprocessing'
if "$cc" -std=gnu11 -Werror -fsyntax-only -x c "$work/negative.i" \
    >"$work/negative.gnu.log" 2>&1; then
    fail 'GNU unexpectedly allowed writing a const-promoted field'
fi
if "$minic" --target=x86_64-linux -S "$work/negative.i" \
    -o "$work/negative.s" >"$work/negative.minic.log" 2>&1; then
    fail 'MiniC unexpectedly allowed writing a const-promoted field'
fi
grep -F 'modifiable object lvalue' "$work/negative.minic.log" >/dev/null \
    || fail 'MiniC negative diagnosis was not const-lvalue enforcement'

echo 'ANONYMOUS_QUALIFIED_RECORD=PASS GNU_MiniC_x86_native=1 5ISA_source_assembly=5/5 const_qualifier_reject=1 tag_typedef_memberless=1 nested_extension=1'
