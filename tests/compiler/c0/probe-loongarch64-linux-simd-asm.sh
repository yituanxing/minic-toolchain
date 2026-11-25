#!/bin/sh
# Source-backed LoongArch64 LSX/LASX inline ASM: original Linux 6.6.143
# lib/raid6/{loongarch_simd,recov_loongarch_simd}.c and
# arch/loongarch/lib/xor_simd.c instruction and operand forms.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
fixture=tests/compiler/c0/loongarch64_linux_simd_asm.i
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$MINIC_CC" --target=loongarch64-linux -fPIE -S "$fixture" \
  -o "$work/simd.s"
for instruction in \
    'vld $vr0, $t0, 0' 'vst $vr16, $t0, 0' \
    'xvld $xr20, $t0, 0' 'xvst $xr4, $t0, 0' \
    'xvreplve0.q $xr22, $xr20' 'xvshuf.b $xr2, $xr22, $xr22, $xr4' \
    'vxor.v $vr0, $vr0, $vr4' 'vld $vr1, $t0, 16' \
    'vst $vr1, $t0, 16'; do
    if ! grep -Fq "$instruction" "$work/simd.s"; then
        echo "MINIC_LOONG_SIMD=FAIL missing actual machine instruction $instruction" >&2
        exit 1
    fi
done
test "$(grep -Ec '^  vst \$vr[0-7], \$t0, 0$' "$work/simd.s")" -ge 8
test "$(grep -Ec '^  vld \$vr[0-7], \$t0, 0$' "$work/simd.s")" -ge 9
echo 'MINIC_LOONG_SIMD_POSITIVE=PASS LSX=1 LASX=1 eight_rw_m=8 named_r=2'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/simd.s" -o "$work/simd.o"
    echo 'MINIC_LOONG_SIMD_GNU_AS=PASS real instructions encoded'
else
    echo 'MINIC_LOONG_SIMD_GNU_AS=SKIP missing Loong GNU assembler'
fi

# These mutations keep the original operand types but break real ISA
# grammar, register availability or required immediate semantics. A success
# here would mean the compiler admitted arbitrary assembly text.
for mutation in opcode register immediate unknown_lasx; do
    case "$mutation" in
        opcode) sed 's/vxor\.v/vunknown.v/g' "$fixture" >"$work/bad.i" ;;
        register) sed 's/\$vr0/\$vr33/g' "$fixture" >"$work/bad.i" ;;
        immediate) sed 's/0x1d/0x20/g' "$fixture" >"$work/bad.i" ;;
        unknown_lasx) sed 's/xvst /xvunknown /g' "$fixture" >"$work/bad.i" ;;
    esac
    if "$MINIC_CC" --target=loongarch64-linux -fPIE -S "$work/bad.i" \
        -o "$work/bad.s" >"$work/bad.out" 2>"$work/bad.err"; then
        echo "MINIC_LOONG_SIMD_NEGATIVE=FAIL mutation=$mutation admitted" >&2
        exit 1
    fi
    test ! -e "$work/bad.s"
done
echo 'MINIC_LOONG_SIMD_NEGATIVE=PASS opcode=reject register=reject immediate=reject lasx=reject'
