#!/usr/bin/env bash
# MC-049: a sizeof(array) in a chained designator can grow Program.array_types.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT
fail() { echo "MC049_STATIC_ARRAY_ARENA=FAIL $*" >&2; exit 1; }

test -x "$minic" || fail 'MiniC binary missing'

check_variant() {
    local name=$1 define=$2 gnu_status=0 minic_status=0 target
    local -a flags=()
    if test -n "$define"; then flags+=("$define"); fi

    "$cc" -std=gnu11 -E -P "${flags[@]}" \
        "$root/tests/compiler/audit/mc049_static_chained_array_uaf.c" \
        -o "$work/$name.i" || fail "$name GNU preprocessing"
    "$cc" -std=gnu11 -O0 -no-pie -x c "$work/$name.i" -o "$work/$name.gnu" ||
        fail "$name GNU native compile"

    # The x86 capacity boundary is at 29 typedefs; other targets were
    # originally observed at 30. Also exercise the non-growing 31 control.
    for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}" \
            "$minic" --target="$target" -S "$work/$name.i" \
            -o "$work/$name.$target.s" || fail "$name MiniC $target compile"
    done
    "$cc" -no-pie -x assembler "$work/$name.x86_64-linux.s" \
        -o "$work/$name.minic" || fail "$name MiniC native link"
    "$work/$name.gnu" || gnu_status=$?
    "$work/$name.minic" || minic_status=$?
    test "$gnu_status" -eq 7 || fail "$name GNU exit $gnu_status, expected 7"
    test "$minic_status" -eq "$gnu_status" ||
        fail "$name GNU=$gnu_status MiniC=$minic_status"
}

check_variant growth29 -DMINIC_DROP30
check_variant growth30 ''
check_variant control31 -DMINIC_EXTRA31
echo 'MC049_STATIC_ARRAY_ARENA=PASS GNU_MiniC_x86_native_7_and_five_target_29_30_31'
