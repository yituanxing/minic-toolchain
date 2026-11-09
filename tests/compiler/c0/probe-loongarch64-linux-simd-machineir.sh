#!/bin/sh
# Typed MachineIR provenance regression for Linux 6.6 xor_simd pointers.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
fixture=tests/compiler/c0/loongarch64_linux_simd_machineir.i
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$MINIC_CC" --target=loongarch64-linux -S "$fixture" -o "$work/lsx-lasx.s"
for instruction in \
    'vld $vr0, $t0, 0' 'vld $vr1, $t0, 16' \
    'vxor.v $vr0, $vr0, $vr4' 'vst $vr1, $t0, 16' \
    'xvld $xr0, $t0, 0' 'xvxor.v $xr0, $xr0, $xr4' \
    'xvst $xr0, $t0, 0'; do
    grep -Fq "$instruction" "$work/lsx-lasx.s"
done
test "$(grep -c '^  vxor.v ' "$work/lsx-lasx.s")" -eq 2
test "$(grep -c '^  xvxor.v ' "$work/lsx-lasx.s")" -eq 1
echo 'MINIC_LOONG_SIMD_MACHINEIR=PASS LSX=1 LASX=1 named-pointer=1'

# Keep real source pointers/operands, but reject a non-admitted memory offset.
sed 's/\[v1\], 16/\[v1\], 24/g' "$fixture" > "$work/invalid-offset.i"
if "$MINIC_CC" --target=loongarch64-linux -S \
    "$work/invalid-offset.i" -o "$work/invalid-offset.s" \
    >"$work/invalid-offset.out" 2>"$work/invalid-offset.err"; then
    echo 'MINIC_LOONG_SIMD_MACHINEIR_NEGATIVE=FAIL invalid offset admitted' >&2
    exit 1
fi
test ! -e "$work/invalid-offset.s"
echo 'MINIC_LOONG_SIMD_MACHINEIR_NEGATIVE=PASS invalid-offset=reject'
