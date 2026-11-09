#!/bin/sh
# MC-002: nested record in a function-pointer field parameter may grow the
# Record arena. MC-061's RecordId re-acquisition must protect this route too.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}
export ASAN_OPTIONS
source="$root/tests/compiler/audit/mc002_record_field_function_pointer_uaf.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

for phase in 14 15 16; do
    case "$phase" in
        14) defines='-DMC002_SKIP_R15' ;;
        15) defines='' ;;
        16) defines='-DMC002_ADD_R16' ;;
    esac
    "$host_cc" -std=gnu11 -E -P $defines "$source" -o "$work/$phase.i"
    # GCC accepts the prototype-scope record definition but warns that its
    # tag does not escape the parameter list. This is expected and harmless.
    "$host_cc" -std=gnu11 -w -no-pie -x c "$work/$phase.i" -o "$work/$phase.gnu"
    "$minic_cc" --target=x86_64-linux -S "$work/$phase.i" -o "$work/$phase.x86.s"
    "$host_cc" -no-pie -x assembler "$work/$phase.x86.s" -o "$work/$phase.mini"

    set +e
    "$work/$phase.gnu"
    gnu_exit=$?
    "$work/$phase.mini"
    mini_exit=$?
    set -e
    if [ "$gnu_exit" -ne 7 ] || [ "$mini_exit" -ne "$gnu_exit" ]; then
        echo "MC002_RECORD_FIELD=FAIL phase=$phase GNU=$gnu_exit MiniC=$mini_exit" >&2
        exit 1
    fi
    for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        "$minic_cc" --target="$target" -S "$work/$phase.i" -o "$work/$phase.$target.s"
        test -s "$work/$phase.$target.s"
    done
done

"$host_cc" -std=gnu11 -E -P -DMC002_SKIP_R15 -DMC002_DUPLICATE_FIELD \
    "$source" -o "$work/duplicate.i"
if "$host_cc" -std=gnu11 -w -fsyntax-only -x c "$work/duplicate.i" \
    >"$work/gnu-reject.log" 2>&1; then
    echo 'MC002_RECORD_FIELD=FAIL GNU accepted a duplicate member' >&2
    exit 1
fi
if "$minic_cc" --target=x86_64-linux -S "$work/duplicate.i" \
    -o "$work/duplicate.s" >"$work/minic-reject.log" 2>&1; then
    echo 'MC002_RECORD_FIELD=FAIL MiniC accepted a duplicate member' >&2
    exit 1
fi
if grep -Eq 'AddressSanitizer|heap-use-after-free' "$work/minic-reject.log"; then
    echo 'MC002_RECORD_FIELD=FAIL sanitizer failure during duplicate rejection' >&2
    exit 1
fi

echo 'MC002_RECORD_FIELD=PASS 5_targets_x_3_record_phases GNU_MiniC_x86_native=7 duplicate_field=REJECT ASAN_if_enabled=NO_UAF'
