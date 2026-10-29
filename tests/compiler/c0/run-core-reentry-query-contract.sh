#!/bin/sh
# Typed CFG reentry policies: indexed/fallback graph oracle and five-ISA source lowering.
set -eu

: "${MINIC:?Set MINIC to the absolute path of minic-cc}"
: "${CC:=cc}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/core-reentry-query-contract"}
mkdir -p "$work"

"$CC" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror \
    -I"$root/src" -I"$root/include" \
    "$root/tests/core/core_reentry_query_test.c" \
    "$root/src/core/reentry_index.c" -o "$work/core_reentry_query_test"
"$work/core_reentry_query_test"

for name in core_reentry_policy_runtime m144_unreferenced_loop_label_metadata; do
    source_file="$root/tests/compiler/c0/$name.c"
    "$CC" -std=gnu11 -E -P "$source_file" -o "$work/$name.i"
    for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        "$MINIC" --target="$target" -S "$work/$name.i" \
            -o "$work/$name-$target.s"
    done
    "$CC" -std=gnu11 -O0 -Wno-switch-unreachable "$source_file" \
        -o "$work/$name-gcc"
    "$CC" -no-pie "$work/$name-x86_64-linux.s" -o "$work/$name-minic"
    "$work/$name-gcc"
    "$work/$name-minic"
done

printf 'PASS Core typed reentry indexed/fallback, GCC native, MiniC native, 5 ISA -S\n'
