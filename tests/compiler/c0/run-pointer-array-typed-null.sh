#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-pointer-array-typed-null"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/pointer_array_typed_null.c" -o "$work/positive.s"
grep -F 'envp_init:' "$work/positive.s" >/dev/null
grep -F '__minic_static_local_' "$work/positive.s" >/dev/null
relocations=$(grep -c '^  \.dword ' "$work/positive.s")
if [ "$relocations" -lt 3 ]; then
    printf '%s\n' "FAIL compiler/c0/pointer-array-typed-null: missing string relocations" >&2
    exit 1
fi

"$minic" -S "$root/tests/compiler/c0/pointer_array_nonnull_cast.c" -o "$work/nonnull.s"
grep -F 'bad:' "$work/nonnull.s" >/dev/null
grep -F '  .dword 1' "$work/nonnull.s" >/dev/null

printf '%s\n' \
    "PASS compiler/c0/pointer-array-typed-null linkage=static+external+static-local null='0,(void*)0' integer-cast-static=1 semantic=typed"
