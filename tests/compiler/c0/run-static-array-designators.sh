#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-array-designators
asm="$work/static_array_designators.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_array_designators.c" \
    -o "$work/static_array_designators.i"
"$minic" -S "$work/static_array_designators.i" -o "$asm"

grep -F 'zero_table:' "$asm" >/dev/null
grep -F '.size zero_table, 32' "$asm" >/dev/null
grep -F 'indexed:' "$asm" >/dev/null
grep -F '.size indexed, 32' "$asm" >/dev/null
grep -F 'ranged:' "$asm" >/dev/null
grep -F '.size ranged, 32' "$asm" >/dev/null
grep -F 'mutable_inferred:' "$asm" >/dev/null
grep -F '.size mutable_inferred, 20' "$asm" >/dev/null
grep -F 'names:' "$asm" >/dev/null
grep -F '.size names, 24' "$asm" >/dev/null

sed -n '/^functions:/,/^.size functions, 32/p' "$asm" | \
    grep '^  \.dword ' >"$work/functions.actual"
cat >"$work/functions.expected" <<'EOF'
  .dword real0
  .dword fallback
  .dword real2
  .dword fallback
EOF
diff -u "$work/functions.expected" "$work/functions.actual"

sed -n '/^names:/,/^.size names, 24/p' "$asm" | \
    grep '^  \.dword ' >"$work/names.actual"
test "$(wc -l <"$work/names.actual")" -eq 3
test "$(grep -c '^  \.dword \.Lminic_string_' "$work/names.actual")" -eq 2
grep -F '  .dword 0' "$work/names.actual" >/dev/null

expect_failure() {
    name=$1
    message=$2
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$message" "$work/$name.stderr" >/dev/null
}

expect_failure invalid_static_array_designator_oob \
    'array designator index is outside the initialized array'
expect_failure invalid_static_array_designator_range \
    'GNU array range designator upper bound is below lower bound'
expect_failure invalid_static_array_designator_nonconstant \
    'array designator requires an integer constant expression'

printf '%s\n' \
    'PASS compiler/c0/static_array_designators c99=1 gnu-range=1 overwrite=1 function-reloc=4 string-reloc=2 inferred=1 writable-zero=1'
