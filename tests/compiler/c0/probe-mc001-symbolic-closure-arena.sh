#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/mc001_symbolic_closure_arena.c"

# The original bug is an Expression-arena realloc within a recursive
# symbolic closure.  PAD=13 hit the ASan UAF in the unfixed compiler.
# Keep both a single symbolic argument and a two-argument case: the latter
# also checks that the outer CALL is reacquired after the first argument.
for variant in single double; do
    for pad in 0 12 13 14 29 30 31 62 63; do
        if test "$variant" = double; then
            set -- -DMC001_TWO_ARGUMENTS
        else
            set --
        fi
        "$host_cc" -std=gnu11 -Wall -Wextra -Werror -E -P "$@" \
            -DPAD_COUNT="$pad" "$source" -o "$work/source.i"
        for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
            ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}" \
                "$minic_cc" --target="$target" -S "$work/source.i" \
                -o "$work/source-$target.s" 2>"$work/minic.log"
            test -s "$work/source-$target.s"
            if test "$target" = x86_64-linux; then
                grep -Eq 'MINIC_INLINE_SYMBOLIC_SPECIALIZATION_V0.*transitive=[1-9]' \
                    "$work/minic.log"
            fi
        done

        if test "$pad" = 0 || test "$pad" = 13 || test "$pad" = 30; then
            "$host_cc" -std=gnu11 -Wall -Wextra -Werror -no-pie "$@" \
                -DPAD_COUNT="$pad" "$source" -o "$work/gnu"
            "$host_cc" -no-pie -x assembler "$work/source-x86_64-linux.s" \
                -o "$work/minic"
            "$work/gnu"
            "$work/minic"
        fi
    done
done

echo 'MC001_SYMBOLIC_CLOSURE_ARENA=PASS variants=2 five_target_S=90 native_GNU_MiniC=6'
