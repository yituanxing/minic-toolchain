#!/bin/sh
# MC-039: capacity phases around Program.array_types growth. Also run with
# a MODE=sanitize minic-cc: any parser heap-use-after-free is a test failure.
set -eu
# LSan cannot inspect /proc tasks in some sandboxed runners; this is an
# AddressSanitizer UAF probe, so retain ASan trapping while skipping LSan.
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}
export ASAN_OPTIONS

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/runtime_nested_array_arena_growth.c"

for phase in prefix13 prefix14 prefix15 prefix16; do
    case "$phase" in
        prefix13) defines='-DMC039_SKIP_LAST_PREFIX' ;;
        prefix14) defines='' ;;
        prefix15) defines='-DMC039_EXTRA_PREFIX' ;;
        prefix16) defines='-DMC039_EXTRA_PREFIX -DMC039_EXTRA_PREFIX_2' ;;
    esac
    # GNU and MiniC see the identical preprocessed translation unit.
    "$host_cc" -std=gnu11 -E -P $defines "$source" -o "$work/$phase.i"
    "$host_cc" -std=gnu11 -Werror -O0 -no-pie \
        -x c "$work/$phase.i" -o "$work/$phase.gnu"
    "$work/$phase.gnu"

    for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        "$minic_cc" --target="$target" -S "$work/$phase.i" -o "$work/$phase.$target.s"
        test -s "$work/$phase.$target.s"
    done
    "$host_cc" -no-pie -x assembler "$work/$phase.x86_64-linux.s" \
        -o "$work/$phase.minic"
    "$work/$phase.minic"
done

echo 'MC039_NESTED_ARRAY_ARENA=PASS phases=13,14,15,16 five_targets_S=20 GNU_MiniC_x86_native=4x2'
