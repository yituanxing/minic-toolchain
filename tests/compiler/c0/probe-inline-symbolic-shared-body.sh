#!/bin/sh
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -E -P -x c "$root/tests/compiler/c0/inline_symbolic_shared_body.c" \
    -o "$work/source.i"
"$MINIC_CC" --target=x86_64-linux -S "$work/source.i" \
    -o "$work/source.s" 2> "$work/minic.log"
grep -Eq 'MINIC_INLINE_SYMBOLIC_SPECIALIZATION_V0.*transitive=[1-9]' \
    "$work/minic.log"
${CC:-cc} -no-pie "$work/source.s" -o "$work/minic"
"$work/minic"
${CC:-cc} -O0 "$root/tests/compiler/c0/inline_symbolic_shared_body.c" \
    -o "$work/gnu"
"$work/gnu"
echo 'INLINE_SYMBOLIC_SHARED_BODY=PASS transitive_clones_distinct=1 native_oracle=1'
