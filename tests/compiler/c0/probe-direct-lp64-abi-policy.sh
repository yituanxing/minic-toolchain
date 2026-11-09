#!/bin/sh
# Compile-only ABI behavior gate; GNU AS/QEMU runtime acceptance is separate.
set -eu
: "${MINIC_CC:=build/debug/bin/minic-cc}"
work=$(mktemp -d)
trap 'rm -f "$work/aarch64.s" "$work/loongarch64.s" "$work/aarch64-neg.s" "$work/loongarch64-neg.s" "$work/loongarch64.err" "$work/policy.i" "$work/negative.i"; rmdir "$work"' EXIT HUP INT TERM

cc -E -P -x c tests/compiler/c0/probe-direct-lp64-abi-policy.c \
    -o "$work/policy.i"
cc -E -P -x c tests/compiler/c0/probe-direct-lp64-abi-policy-negative.c \
    -o "$work/negative.i"

for arch in aarch64 loongarch64; do
    "$MINIC_CC" --target="$arch-linux" -S \
        "$work/policy.i" \
        -o "$work/$arch.s"
    test -s "$work/$arch.s"
    echo "DIRECT_LP64_ABI_POLICY=PASS arch=$arch shapes=record,int128,indirect,fp,varargs"
done

"$MINIC_CC" --target=aarch64-linux -S \
    "$work/negative.i" \
    -o "$work/aarch64-neg.s"
test -s "$work/aarch64-neg.s"
if MINIC_M1_TRACE=1 "$MINIC_CC" --target=loongarch64-linux -S \
    "$work/negative.i" \
    -o "$work/loongarch64-neg.s" 2>"$work/loongarch64.err"; then
    echo 'DIRECT_LP64_ABI_POLICY=FAIL arch=loongarch64 rejected-fixed-double' >&2
    exit 1
fi
test ! -e "$work/loongarch64-neg.s"
grep -q 'stage=variadic_fp_fixed' "$work/loongarch64.err"
echo 'DIRECT_LP64_ABI_POLICY=PASS named-double-variadic=aarch64-only'
