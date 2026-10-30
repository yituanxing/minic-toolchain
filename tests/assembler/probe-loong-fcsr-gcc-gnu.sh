#!/usr/bin/env bash
# Linux 6.6.143 emits $r0 rather than $fcsr0 for LoongArch FCSR operands.
# Verify that this GAS spelling keeps the FCSR register bank, exact object
# bytes, and strict two-bit FCSR range -- without GNU-AS fallback.
set -Eeuo pipefail

: "${MINIAS:?provide MiniAS executable}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/gcc-fcsr.s" <<'ASM'
.text
.globl kernel_fcsr_sample
kernel_fcsr_sample:
    movfcsr2gr $r5, $r0
    movgr2fcsr $r0, $r12
    movfcsr2gr $r13, $r0
    movgr2fcsr $r0, $r20
ASM

cat >"$work/canonical-fcsr.s" <<'ASM'
.text
.globl kernel_fcsr_sample
kernel_fcsr_sample:
    movfcsr2gr $r5, $fcsr0
    movgr2fcsr $fcsr0, $r12
    movfcsr2gr $r13, $fcsr0
    movgr2fcsr $fcsr0, $r20
ASM

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64s \
    -o "$work/mini.o" "$work/gcc-fcsr.s"
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64s \
    -o "$work/canonical.o" "$work/canonical-fcsr.s"

if [[ ${MINIAS_LOONG_SKIP_GNU:-0} != 1 ]]; then
    : "${LOONGAS:?provide GNU LoongArch assembler}"
    : "${LOONGCOPY:?provide GNU LoongArch objcopy}"
    "$LOONGAS" -mabi=lp64s -o "$work/gnu.o" "$work/gcc-fcsr.s"
    for variant in mini canonical gnu; do
        "$LOONGCOPY" -O binary -j .text "$work/$variant.o" "$work/$variant.bin"
    done
    cmp "$work/mini.bin" "$work/gnu.bin"
    cmp "$work/canonical.bin" "$work/gnu.bin"
else
    cmp "$work/mini.o" "$work/canonical.o"
fi

for invalid in \
    'movfcsr2gr $r5, $r4' \
    'movgr2fcsr $r4, $r12' \
    'movfcsr2gr $r5, $r31' \
    'movgr2fcsr $r31, $r12' \
    'movgr2fcsr $fcsr4, $r12' \
    'movfcsr2gr $f1, $r0'; do
    printf '.text\n%s\n' "$invalid" >"$work/invalid.s"
    if "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64s \
        -o "$work/invalid.o" "$work/invalid.s" >/dev/null 2>&1; then
        echo "MINIAS_LOONG_FCSR_GCC=FAIL accepted_invalid=$invalid" >&2
        exit 1
    fi
done

if [[ ${MINIAS_LOONG_SKIP_GNU:-0} == 1 ]]; then
    echo 'MINIAS_LOONG_FCSR_GCC=PASS GNU_bytes=NOT_TESTED instructions=4 invalid_rejected=6'
else
    echo 'MINIAS_LOONG_FCSR_GCC=PASS GNU_bytes=exact instructions=4 invalid_rejected=6'
fi
