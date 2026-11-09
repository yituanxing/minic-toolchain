#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mini=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host=${HOST_CC:-cc}
source="$root/tests/compiler/c0/core_cfg_effectful_boolean_fact_barrier.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

"$host" -E -P -std=gnu11 "$source" -o "$work/source.i"
"$host" -std=gnu11 -O2 -S -x c "$work/source.i" -o "$work/gnu.s"
for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    "$mini" --target="$target" -S "$work/source.i" -o "$work/$target.s"
    if grep -Fq '__compiletime_assert_impossible' "$work/$target.s"; then
        echo "EFFECTFUL_BOOLEAN_FACT_BARRIER=FAIL static_guard:$target" >&2
        exit 1
    fi
done
if grep -Fq '__compiletime_assert_impossible' "$work/gnu.s"; then
    echo 'EFFECTFUL_BOOLEAN_FACT_BARRIER=FAIL GNU_static_guard' >&2
    exit 1
fi
"$host" -c -x assembler "$work/gnu.s" -o "$work/gnu.o"
"$host" -c -x assembler "$work/x86_64-linux.s" -o "$work/minic.o"
if nm -u "$work/gnu.o" "$work/minic.o" | grep -Fq '__compiletime_assert_impossible'; then
    echo 'EFFECTFUL_BOOLEAN_FACT_BARRIER=FAIL native_ELF_static_guard' >&2
    exit 1
fi
"$host" -no-pie "$work/gnu.o" -o "$work/gnu"
"$host" -no-pie "$work/minic.o" -o "$work/minic"
set +e
"$work/gnu"; gnu_rc=$?
"$work/minic"; minic_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$minic_rc" -ne "$gnu_rc" ]; then
    echo "EFFECTFUL_BOOLEAN_FACT_BARRIER=FAIL GNU=$gnu_rc MiniC=$minic_rc" >&2
    exit 1
fi
printf 'EFFECTFUL_BOOLEAN_FACT_BARRIER=PASS five_ISA_S=5 GNU_AS=2 GNU_runtime=%s MiniC_runtime=%s stale_calls=4 stale_CFG=3 effectful_if_switch=2 stmt_outer_local=1 pure_static_effects=2 sibling=1\n' \
    "$gnu_rc" "$minic_rc"
