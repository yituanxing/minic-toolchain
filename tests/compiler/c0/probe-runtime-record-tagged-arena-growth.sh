#!/bin/sh
# MC-003: tagged nested record in runtime positional initializer; ASan
# catches the historical Program.records pointer retained across recursion.
set -eu
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}
export ASAN_OPTIONS

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/audit/mc003_runtime_record_tagged_uaf.c"

for phase in prefix14 prefix15 prefix16; do
    case "$phase" in
        prefix14) defines='-DMINIC_SKIP_R15' ;;
        prefix15) defines='' ;;
        prefix16) defines='-DMINIC_ADD_R16' ;;
    esac
    "$host_cc" -std=gnu11 -E -P $defines "$source" -o "$work/$phase.i"
    "$host_cc" -std=gnu11 -Werror -O0 -no-pie -x c "$work/$phase.i" -o "$work/$phase.gnu"
    gnu_exit=0
    "$work/$phase.gnu" || gnu_exit=$?
    if [ "$gnu_exit" -ne 26 ]; then
        echo "MC003_RECORD_TAGGED=FAIL phase=$phase GNU_exit=$gnu_exit" >&2
        exit 1
    fi
    for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        "$minic_cc" --target="$target" -S "$work/$phase.i" -o "$work/$phase.$target.s"
        test -s "$work/$phase.$target.s"
    done
    "$host_cc" -no-pie -x assembler "$work/$phase.x86_64-linux.s" -o "$work/$phase.minic"
    minic_exit=0
    "$work/$phase.minic" || minic_exit=$?
    if [ "$minic_exit" -ne "$gnu_exit" ]; then
        echo "MC003_RECORD_TAGGED=FAIL phase=$phase GNU=$gnu_exit MiniC=$minic_exit" >&2
        exit 1
    fi
done

echo 'MC003_RECORD_TAGGED=PASS five_targets_x_3_record_phases GNU_MiniC_x86_native=26 ASAN_if_enabled=NO_UAF'
