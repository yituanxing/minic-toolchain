#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-compatible-incomplete-array-pointer

mkdir -p "$work"
"$host_cc" -std=c11 -pedantic-errors -Werror=incompatible-pointer-types \
    -fsyntax-only "$root/tests/compiler/c0/compatible_incomplete_array_pointer.c"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/compatible_incomplete_array_pointer.c" -o "$work/good.i"
MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -S "$work/good.i" -o "$work/good.s"
"$minic_cc" --target=aarch64-linux -S "$work/good.i" -o "$work/arm.s"
"$minic_cc" --target=loongarch64-linux -S "$work/good.i" -o "$work/loong.s"
"$host_cc" -no-pie "$work/good.s" -o "$work/good"
"$work/good"

for stem in invalid_complete_array_pointer_bound invalid_incomplete_array_pointer_discard_const; do
    if "$host_cc" -std=c11 -pedantic-errors -Werror=incompatible-pointer-types \
        -fsyntax-only "$root/tests/compiler/c0/$stem.c" \
        >"$work/$stem.gnu.stdout" 2>"$work/$stem.gnu.stderr"; then
        echo "FAIL GCC accepted $stem" >&2
        exit 1
    fi
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$stem.c" -o "$work/$stem.i"
    if "$minic_cc" --target=x86_64-linux -S "$work/$stem.i" -o "$work/$stem.s" \
        >"$work/$stem.stdout" 2>"$work/$stem.stderr"; then
        echo "FAIL MiniC accepted $stem" >&2
        exit 1
    fi
    grep -F 'static pointer initializer type mismatch' "$work/$stem.stderr" >/dev/null
done

printf '%s\n' 'PASS compiler/c0/compatible_incomplete_array_pointer complete-vs-incomplete=1 compound-literal=1 fixed-bound-mismatch=rejected const-drop=rejected x86-native=1 targets=3 strict-core=1'
