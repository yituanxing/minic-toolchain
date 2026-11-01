#!/bin/sh
# MC-035: capacity-triggered Program.expressions UAF in the range-once Local
# materialization path. Run with both Release and MODE=sanitize minic-cc.
set -eu
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1:abort_on_error=1}
export ASAN_OPTIONS

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
source="$root/tests/compiler/c0/mc035_runtime_range_once_arena.c"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM

for phase in 0 1 2 3 4 5; do
    "$host_cc" -std=gnu11 -E -P -DMC035_PREFIX_PHASE="$phase" \
        "$source" -o "$work/phase$phase.i"
    "$host_cc" -std=gnu11 -Werror -O0 -no-pie -x c \
        "$work/phase$phase.i" -o "$work/phase$phase.gnu"
    "$work/phase$phase.gnu"

    for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        "$minic_cc" --target="$target" -S "$work/phase$phase.i" \
            -o "$work/phase$phase.$target.s"
        test -s "$work/phase$phase.$target.s"
    done
    "$host_cc" -no-pie -x assembler "$work/phase$phase.x86_64-linux.s" \
        -o "$work/phase$phase.minic"
    "$work/phase$phase.minic"
done

echo 'MC035_RUNTIME_ONCE_ARENA=PASS phases=0..5 five_target_S=30 GNU_MiniC_x86_native=6x2 once=1'
