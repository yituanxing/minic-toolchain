#!/bin/sh
# MC-061: pointer-to-array field declaration recursively appends anonymous
# records. Vary the arena capacity phase; require GNU/MiniC equality and no
# sanitizer UAF when invoked with a MODE=sanitize compiler.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
# Some CI/PTY environments prohibit LeakSanitizer's ptrace attach. UAF and
# UBSan remain active; disable only the unrelated leak collector for this gate.
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}
export ASAN_OPTIONS
source="$root/tests/compiler/audit/mc061_record_field_pointer_array_uaf.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

for phase in 14 15 16; do
    case "$phase" in
        14) defines='-DMINIC_SKIP_R15' ;;
        15) defines='' ;;
        16) defines='-DMINIC_ADD_R16' ;;
    esac
    "$host_cc" -std=gnu11 -E -P $defines "$source" -o "$work/phase-$phase.i"
    "$host_cc" -std=gnu11 -Wall -Wextra -Werror -no-pie -x c "$work/phase-$phase.i" -o "$work/phase-$phase.gnu"
    "$minic_cc" --target=x86_64-linux -S "$work/phase-$phase.i" -o "$work/phase-$phase.x86.s"
    "$host_cc" -no-pie -x assembler "$work/phase-$phase.x86.s" -o "$work/phase-$phase.mini"

    set +e
    "$work/phase-$phase.gnu"
    gnu_exit=$?
    "$work/phase-$phase.mini"
    mini_exit=$?
    set -e
    if [ "$gnu_exit" -ne 12 ] || [ "$mini_exit" -ne "$gnu_exit" ]; then
        echo "MC061_RECORD_FIELD=FAIL phase=$phase GNU=$gnu_exit MiniC=$mini_exit" >&2
        exit 1
    fi
    for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        "$minic_cc" --target="$target" -S "$work/phase-$phase.i" -o "$work/phase-$phase.$target.s"
        test -s "$work/phase-$phase.$target.s"
    done
done

"$host_cc" -std=gnu11 -E -P -DMINIC_SKIP_R15 -DMC061_DUPLICATE_FIELD "$source" -o "$work/duplicate.i"
if "$host_cc" -std=gnu11 -Werror -fsyntax-only -x c "$work/duplicate.i" >"$work/gnu-reject.log" 2>&1; then
    echo 'MC061_RECORD_FIELD=FAIL GNU accepted duplicate member' >&2
    exit 1
fi
if "$minic_cc" --target=x86_64-linux -S "$work/duplicate.i" -o "$work/duplicate.s" >"$work/minic-reject.log" 2>&1; then
    echo 'MC061_RECORD_FIELD=FAIL MiniC accepted duplicate member' >&2
    exit 1
fi
if grep -Eq 'AddressSanitizer|heap-use-after-free' "$work/minic-reject.log"; then
    echo 'MC061_RECORD_FIELD=FAIL sanitizer failure during duplicate rejection' >&2
    exit 1
fi

echo 'MC061_RECORD_FIELD=PASS 5_targets_x_3_record_phases GNU_MiniC_x86_native=12 duplicate_field=REJECT ASAN_if_enabled=NO_UAF'
