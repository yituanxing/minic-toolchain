#!/bin/sh
# Exercise nonzero, late source ASM ids and repeated lookalike machine forms.
# The SIMD emitter must use the actual lowered statement's provenance rather
# than rescanning all source inline ASM and accepting any matching candidate.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
fixture=tests/compiler/c0/loongarch64_linux_simd_asm.i
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

{
    printf 'void preceding_inline_asms(unsigned char *p) {\n'
    i=0
    while test "$i" -lt 128; do
        # One is intentionally identical to the first real SIMD source form;
        # a whole-translation-unit matching scan cannot prove ownership.
        printf '  asm volatile("vld $vr0, %%0" : : "m"(*p));\n'
        i=$((i + 1))
    done
    printf '}\n'
    cat "$fixture"
} > "$work/late-source.i"

"$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/late-source.i" -o "$work/late-source.s"

test "$(grep -Ec '^  vld \$vr0, \$t0, 0$' "$work/late-source.s")" -ge 130
grep -Fq '  xvreplve0.q $xr22, $xr20' "$work/late-source.s"
grep -Fq '  xvshuf.b $xr2, $xr22, $xr22, $xr4' "$work/late-source.s"
grep -Fq '  vxor.v $vr0, $vr0, $vr4' "$work/late-source.s"
echo 'MINIC_LOONG_SIMD_PROVENANCE=PASS decoys=128 repeated-template=128 late-source=1'
