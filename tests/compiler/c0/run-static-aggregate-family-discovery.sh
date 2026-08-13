#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/static-aggregate-family-discovery"}
mkdir -p "$build_dir"

"$minic" -S "$root/tests/compiler/c0/static_record_compound_literal.c" \
    -o "$build_dir/static_record_compound_literal.s"
grep -F '.word -559067475' "$build_dir/static_record_compound_literal.s" >/dev/null
grep -F '.dword -1' "$build_dir/static_record_compound_literal.s" >/dev/null
test "$(grep -c '  .dword value+' "$build_dir/static_record_compound_literal.s")" -eq 2
if "$minic" -S "$root/tests/compiler/c0/invalid_static_record_compound_literal_type.c" \
    -o "$build_dir/invalid.s" >"$build_dir/invalid.stdout" 2>"$build_dir/invalid.stderr"; then
    echo 'FAIL static aggregate discovery: mismatched record compound literal accepted' >&2
    exit 1
fi
grep -F 'static record compound literal type mismatch' "$build_dir/invalid.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/static-aggregate-family compound-literal=record designated-inner=shared mismatch=fail-closed'
