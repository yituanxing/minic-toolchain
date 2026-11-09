#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc=${HOST_CC:-cc}
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
fixture="$root/tests/compiler/c0/riscv_fixed_register_phase_alias.c"

"$cc" -E -P -std=gnu11 "$fixture" -o "$work/positive.i"
for target in riscv32 riscv64; do
    "$minic" --target="$target-linux" -S "$work/positive.i" \
        -o "$work/$target-positive.s"
    grep -E 'add[[:space:]]+(a0|x10),[[:space:]]*(a0|x10),[[:space:]]*(a1|x11)' \
        "$work/$target-positive.s" >/dev/null
    grep -E '# nr=(a7|x17)' "$work/$target-positive.s" >/dev/null
    for bad in EARLYCLOBBER TWO_INPUTS; do
        "$cc" -E -P -std=gnu11 -DMINIC_BAD_"$bad" "$fixture" \
            -o "$work/$target-$bad.i"
        if "$minic" --target="$target-linux" -S "$work/$target-$bad.i" \
                -o "$work/$target-$bad.s" \
                >"$work/$target-$bad.stdout" 2>"$work/$target-$bad.stderr"; then
            echo "FAIL RISC-V fixed phase alias accepted $target $bad" >&2
            exit 1
        fi
        test ! -s "$work/$target-$bad.s"
    done
    echo "RISCV_FIXED_PHASE_ALIAS=PASS target=$target original_vdso_overlap=1 earlyclobber_rejected=1 two_inputs_rejected=1"
done
