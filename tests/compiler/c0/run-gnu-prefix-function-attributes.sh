#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-prefix-function-attributes
assembly="$work/gnu_prefix_function_attributes.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_prefix_function_attributes.c" \
    -o "$work/gnu_prefix_function_attributes.i"
"$minic" -S "$work/gnu_prefix_function_attributes.i" -o "$assembly"

test -s "$assembly"
grep -F 'prefix_attribute_identity:' "$assembly" >/dev/null
grep -F 'call_prefix_attribute_identity:' "$assembly" >/dev/null
grep -F 'externally_visible_decl:' "$assembly" >/dev/null
grep -F '.globl externally_visible_decl' "$assembly" >/dev/null
grep -F '.section .probe.externally-visible.text' "$assembly" >/dev/null
grep -F 'externally_visible_object' "$assembly" >/dev/null
if grep -F '.hidden externally_visible_decl' "$assembly" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/gnu_prefix_function_attributes: externally_visible was mapped to ELF hidden visibility' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/gnu_prefix_function_attributes prefix=1 metadata=unused,no-instrument,externally-visible function+object=1 reachability=parse-only public-linkage=preserved gnu-inline=static-only'
