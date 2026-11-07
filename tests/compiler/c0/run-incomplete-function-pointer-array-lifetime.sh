#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-incomplete-fnptr-array
mkdir -p "$work"

"$host_cc" -std=gnu11 -Wall -Wextra -Werror \
    "$root/tests/compiler/c0/incomplete_function_pointer_array_lifetime.c" \
    -o "$work/gnu"
"$work/gnu"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/incomplete_function_pointer_array_lifetime.c" \
    -o "$work/valid.i"
for shape in direct repeated typedef; do
    source="$root/tests/compiler/c0/incomplete_function_pointer_array_${shape}.c"
    "$host_cc" -std=gnu11 -Wall -Wextra -Werror -c "$source" -o "$work/$shape.gnu.o"
    "$host_cc" -E -P -x c "$source" -o "$work/$shape.i"
done

invalid="$root/tests/compiler/c0/invalid_incomplete_function_pointer_array_redeclaration.c"
if "$host_cc" -std=gnu11 -Wall -Wextra -Werror -c "$invalid" \
    -o "$work/invalid.gnu.o" >"$work/invalid.gnu.out" 2>"$work/invalid.gnu.err"; then
    printf '%s\n' 'FAIL GNU accepted incompatible callback array redeclaration' >&2
    exit 1
fi
"$host_cc" -E -P -x c "$invalid" -o "$work/invalid.i"

for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$minic_cc" "--target=$target-linux" -S "$work/valid.i" -o "$work/valid.$target.s"
    for shape in direct repeated typedef; do
        "$minic_cc" "--target=$target-linux" -S "$work/$shape.i" \
            -o "$work/$shape.$target.s"
    done
    if "$minic_cc" "--target=$target-linux" -S "$work/invalid.i" \
        -o "$work/invalid.$target.s" >"$work/invalid.$target.out" \
        2>"$work/invalid.$target.err"; then
        printf 'FAIL MiniC accepted incompatible callback array: %s\n' "$target" >&2
        exit 1
    fi
done

printf '%s\n' 'PASS compiler/c0/incomplete-function-pointer-array-lifetime GNU + 5ISA direct+repeated+typedef ownership distinct bounds and incompatible reject'
