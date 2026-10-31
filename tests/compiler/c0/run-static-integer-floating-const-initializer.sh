#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-integer-floating-const
mkdir -p "$work"

"$host_cc" -std=gnu11 -Werror -c \
    "$root/tests/compiler/c0/static_integer_floating_const_initializer.c" -o "$work/gnu.o"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/static_integer_floating_const_initializer.c" -o "$work/valid.i"
for bad in runtime overflow; do
    source="$root/tests/compiler/c0/invalid_static_integer_floating_${bad}_initializer.c"
    if "$host_cc" -std=gnu11 -Werror -c "$source" -o "$work/invalid-$bad.o" \
        >"$work/invalid-$bad.gnu.out" 2>"$work/invalid-$bad.gnu.err"; then
        printf 'FAIL GNU accepted invalid floating initializer: %s\n' "$bad" >&2
        exit 1
    fi
    "$host_cc" -E -P -x c "$source" -o "$work/invalid-$bad.i"
done

for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$minic_cc" "--target=$target-linux" -S "$work/valid.i" \
        -o "$work/valid.$target.s"
    for bad in runtime overflow; do
        if "$minic_cc" "--target=$target-linux" -S "$work/invalid-$bad.i" \
            -o "$work/invalid-$bad.$target.s" \
            >"$work/invalid-$bad.$target.out" 2>"$work/invalid-$bad.$target.err"; then
            printf 'FAIL MiniC accepted invalid floating initializer: %s %s\n' "$target" "$bad" >&2
            exit 1
        fi
        grep -F 'integer initializer requires an arithmetic constant' \
            "$work/invalid-$bad.$target.err" >/dev/null
    done
done

printf '%s\n' 'PASS compiler/c0/static-integer-floating-const GNU C differential 5ISA QCOM freq_tbl F(12.5,3.5,2.5) runtime+overflow=reject'
