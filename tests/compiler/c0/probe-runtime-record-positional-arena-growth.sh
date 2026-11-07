#!/bin/sh
# MC-056: capacity phase changes reveal an otherwise intermittent Record UAF.
# Run this probe with MODE=sanitize minic-cc as well as Release.
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
source="$root/tests/compiler/audit/mc056_runtime_record_positional_uaf.c"

for phase in prefix14 prefix15 prefix16; do
    case "$phase" in
        prefix14) defines='-DMINIC_SKIP_R15' ;;
        prefix15) defines='' ;;
        prefix16) defines='-DMINIC_ADD_R16' ;;
    esac
    "$host_cc" -std=gnu11 -E -P $defines "$source" -o "$work/$phase.i"
    "$host_cc" -std=gnu11 -Werror -O0 -no-pie \
        -x c "$work/$phase.i" -o "$work/$phase.gnu"
    gnu_exit=0
    "$work/$phase.gnu" || gnu_exit=$?
    if [ "$gnu_exit" -ne 7 ]; then
        echo "MC056_RECORD_ARENA=FAIL phase=$phase GNU_exit=$gnu_exit" >&2
        exit 1
    fi

    for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        "$minic_cc" --target="$target" -S "$work/$phase.i" -o "$work/$phase.$target.s"
        test -s "$work/$phase.$target.s"
    done
    "$host_cc" -no-pie -x assembler "$work/$phase.x86_64-linux.s" \
        -o "$work/$phase.minic"
    minic_exit=0
    "$work/$phase.minic" || minic_exit=$?
    if [ "$minic_exit" -ne "$gnu_exit" ]; then
        echo "MC056_RECORD_ARENA=FAIL phase=$phase GNU=$gnu_exit MiniC=$minic_exit" >&2
        exit 1
    fi
done

echo 'MC056_RECORD_ARENA=PASS phases=14,15,16 five_targets_S=15 GNU_MiniC_x86_native=3x2_exit7'
