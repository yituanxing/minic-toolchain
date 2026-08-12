#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-preprocessed-line-markers

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/preprocessed_line_markers.i" \
    -o "$work/preprocessed_line_markers.s"

test -s "$work/preprocessed_line_markers.s"
grep -F 'marker_value:' "$work/preprocessed_line_markers.s" >/dev/null
grep -F '  li a0, 7' "$work/preprocessed_line_markers.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_preprocessor_directive.i" -o "$work/invalid.s" >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/preprocessed_line_markers: arbitrary directive was hidden' >&2
    exit 1
fi
grep -F 'unsupported preprocessor directive' "$work/invalid.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/preprocessed_line_markers gcc-numeric=skip arbitrary-directive=not-hidden'
