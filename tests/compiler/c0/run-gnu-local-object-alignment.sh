#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-gnu-local-object-alignment"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_local_object_alignment.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"

grep -F 'local_explicit_alignment:' "$work/output.s" >/dev/null
grep -F 'local_alignof_alignment:' "$work/output.s" >/dev/null
grep -F '  li t1, -32' "$work/output.s" >/dev/null
grep -F '  and sp, sp, t1' "$work/output.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_local_object_alignment frontend=explicit-alignment core=owned rv64=max-natural-explicit dynamic-stack=32 alignof=accepted'
