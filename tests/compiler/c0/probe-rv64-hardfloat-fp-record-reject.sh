#!/bin/sh
# RV64 LP64D named FP aggregates are not raw indirect pointers: psABI
# flattens struct{float}, struct{double} and mixed FP/integer into fa/GAR.
# Until MiniC can marshal that complete classification, fail closed.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source="$root/tests/compiler/c0/rv64_hardfloat_record_abi_probe.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

for kind in FLOAT DOUBLE MIXED; do
    ${CC:-cc} -std=gnu11 -Werror -fsyntax-only \
        -D"PROBE_${kind}" "$source" || {
        echo "RV64_HARDFLOAT_FP_RECORD=FAIL invalid GNU C $kind" >&2; exit 1;
    }
    ${CC:-cc} -E -P -std=gnu11 -D"PROBE_${kind}" -x c "$source" \
        -o "$work/$kind.i"
    if "$MINIC_CC" --target=riscv64-linux -march=rv64imafd -mabi=lp64d \
        -S "$work/$kind.i" -o "$work/$kind.s" \
        >"$work/$kind.out" 2>"$work/$kind.err"; then
        echo "RV64_HARDFLOAT_FP_RECORD=FAIL unsafe ABI accepted $kind" >&2
        exit 1
    fi
    if test -e "$work/$kind.s"; then
        echo "RV64_HARDFLOAT_FP_RECORD=FAIL partial .s emitted $kind" >&2
        exit 1
    fi
    if ! grep -q 'cannot emit RISC-V function' "$work/$kind.err"; then
        echo "RV64_HARDFLOAT_FP_RECORD=FAIL missing diagnosis $kind" >&2
        exit 1
    fi
done

# LP64 softfloat does use raw GP eightbytes for the very same record.
for kind in FLOAT DOUBLE; do
    ${CC:-cc} -E -P -std=gnu11 -D"PROBE_${kind}" -DPROBE_CALLEE_ONLY \
        -x c "$source" -o "$work/$kind-callee.i"
    "$MINIC_CC" --target=riscv64-linux -march=rv64i -mabi=lp64 \
        -S "$work/$kind-callee.i" -o "$work/$kind-soft.s" || {
        echo "RV64_HARDFLOAT_FP_RECORD=FAIL LP64 regression $kind" >&2
        exit 1
    }
done
echo 'RV64_HARDFLOAT_FP_RECORD=PASS LP64D named-FP-record fail_closed=3 LP64 softfloat=2 GNU_C_valid=3'
