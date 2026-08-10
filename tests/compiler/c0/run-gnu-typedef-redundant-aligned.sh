#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-typedef-redundant-aligned
assembly="$work/gnu_typedef_redundant_aligned.s"
nonredundant_assembly="$work/gnu_typedef_nonredundant_aligned.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_typedef_redundant_aligned.c" \
    -o "$work/gnu_typedef_redundant_aligned.i"
"$minic" -S "$work/gnu_typedef_redundant_aligned.i" -o "$assembly"
test -s "$assembly"
grep -F 'signed128_aligned_size:' "$assembly" >/dev/null
grep -F 'aligned_pair_size:' "$assembly" >/dev/null
grep -F '  li a0, 16' "$assembly" >/dev/null
grep -F '  li a0, 32' "$assembly" >/dev/null

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_typedef_nonredundant_aligned.c" \
    -o "$work/gnu_typedef_nonredundant_aligned.i"
"$minic" -S "$work/gnu_typedef_nonredundant_aligned.i" -o "$nonredundant_assembly"
test -s "$nonredundant_assembly"
grep -F 'aligned_holder_size:' "$nonredundant_assembly" >/dev/null
grep -F 'call_single_alignment:' "$nonredundant_assembly" >/dev/null
grep -F '  li a0, 32' "$nonredundant_assembly" >/dev/null
grep -F '  li a0, 16' "$nonredundant_assembly" >/dev/null

cat >"$work/reduced_alignment.c" <<'EOF'
typedef long reduced_long __attribute__((aligned(4)));
reduced_long value;
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/reduced_alignment.c" -o "$work/reduced_alignment.i"
if "$minic" -S "$work/reduced_alignment.i" -o "$work/reduced_alignment.s" \
    >"$work/reduced_alignment.stdout" 2>"$work/reduced_alignment.stderr"; then
    printf '%s\n' 'reducing GNU typedef alignment unexpectedly compiled' >&2
    exit 1
fi
grep -F 'reducing GNU typedef alignment is not supported yet' \
    "$work/reduced_alignment.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/gnu_typedef_redundant_aligned natural-int128=16 overalign-int=16 sizeof-expression=1 record-offset=16 reduction=reject'
