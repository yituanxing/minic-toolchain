#!/bin/sh
# MC-034: static designated initializer must pass an arena-owned Record to
# member-path lookup even when the initializer itself uses a stack snapshot.
set -eu
: "${MINIC_CC:?MINIC_CC must point to minic-cc}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
host_cc=${HOST_CC:-cc}
source_file="$root/tests/compiler/c0/mc034_static_designator_pointer_pair.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

for phase in 0 1 2 3 4 5; do
    "$host_cc" -std=gnu11 -Wall -Wextra -Werror -E -P \
        -DMC034_PREFIX_PHASE="$phase" "$source_file" -o "$work/source.i"
    for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        ASAN_OPTIONS="detect_leaks=0:detect_invalid_pointer_pairs=2:halt_on_error=1:abort_on_error=1" \
            "$MINIC_CC" --target="$target" -S "$work/source.i" \
            -o "$work/$target.s"
        test -s "$work/$target.s"
    done
    "$host_cc" -std=gnu11 -Wall -Wextra -Werror -O2 -no-pie \
        -DMC034_PREFIX_PHASE="$phase" "$source_file" -o "$work/gnu"
    if ! "$host_cc" -no-pie -x assembler "$work/x86_64-linux.s" \
            -o "$work/minic" 2>"$work/assembler.log"; then
        sed -n '1,80p' "$work/assembler.log" >&2
        exit 1
    fi
    "$work/gnu"
    "$work/minic"
done

echo 'MC034_STATIC_DESIGNATOR_POINTER_PAIR=PASS phases=6 five_target_S=30 GNU_MiniC_x86_native=6x2 SAN_pointer_pair_if_enabled=PASS'
