#!/usr/bin/env bash
# LoongArch GPR/FPR/FCSR/FCC, system-control and scalar floating-memory
# instruction families share one target-private operand descriptor; every
# register bank and bounded immediate field is checked before encoding.
set -Eeuo pipefail
trap 'echo "MINIAS_LOONG_REGISTER_BANKS_FAIL line=$LINENO command=$BASH_COMMAND" >&2' ERR
: "${MINIAS:?}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/banks.s" <<'ASM'
.text
.globl loong_register_bank_patterns
loong_register_bank_patterns:
    movgr2fr.d $f0, $r31
    movgr2fr.w $f31, $r0
    movfr2gr.d $r31, $f0
    movfr2gr.d $r0, $f31
    movfcsr2gr $r31, $fcsr0
    movfcsr2gr $r0, $fcsr3
    movgr2fcsr $fcsr0, $r31
    movgr2fcsr $fcsr3, $r0
    movcf2gr $r31, $fcc0
    movcf2gr $r0, $fcc7
    movgr2cf $fcc0, $r31
    movgr2cf $fcc7, $r0
    fld.s $f0, $r31, -2048
    fld.d $f31, $r0, 2047
    fst.s $f0, $r0, 0
    fst.d $f31, $r31, -1
    movgr2fr.d $f30, $a0
    movfr2gr.d $a0, $f30
    movgr2fcsr $fcsr2, $a0
    movgr2cf $fcc3, $a0
    lddir $r0, $r31, 0
    lddir $r31, $r0, 255
    ldpte $r31, 255
    ldpte $r0, 0
    invtlb 0, $r0, $r31
    invtlb 31, $r31, $r0
    cacop 0, $r31, -2048
    cacop 31, $r0, 2047
    csrrd $r31, 0
    csrrd $r0, 16383
    csrwr $r31, 16383
    csrwr $r0, 0
    csrxchg $r31, $r2, 0
    csrxchg $r0, $r31, 16383
    break 0
    break 32767
    syscall 0
    syscall 32767
    dbar 0
    dbar 32767
    ibar 0
    ibar 32767
    idle 0
    idle 32767
ASM
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/mini.o" "$work/banks.s"
if [[ "${MINIAS_LOONG_SKIP_GNU:-0}" != 1 ]]; then
    : "${LOONGAS:?}"
    : "${LOONGCOPY:?}"
    "$LOONGAS" -mabi=lp64d -o "$work/gnu.o" "$work/banks.s"
    "$LOONGCOPY" -j .text -O binary "$work/mini.o" "$work/mini.bin"
    "$LOONGCOPY" -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
    cmp "$work/mini.bin" "$work/gnu.bin"
fi
negatives=(
    'movgr2fr.d $r0, $r1'
    'movgr2fr.w $f32, $r1'
    'movfr2gr.d $r0, $f32'
    'movfr2gr.d $f0, $f1'
    'movfcsr2gr $f1, $fcsr0'
    'movfcsr2gr $r1, $fcsr4'
    'movgr2fcsr $fcsr4, $r1'
    'movgr2fcsr $fcsr1, $f1'
    'movcf2gr $r1, $fcc8'
    'movcf2gr $r1, $f1'
    'movgr2cf $fcc8, $r1'
    'movgr2cf $fcc1, $f0'
    'fld.s $r0, $r1, 0'
    'fld.s $f0, $f1, 0'
    'fld.s $f0, $r1, -2049'
    'fld.d $f0, $r1, 2048'
    'fst.s $f32, $r1, 0'
    'fst.d $f0, $r1, $r2'
    'lddir $r0, $r1, 256'
    'lddir $f1, $r1, 1'
    'ldpte $r1, -1'
    'ldpte $r1, 256'
    'invtlb 32, $r0, $r0'
    'invtlb 0, $f1, $r0'
    'cacop 32, $r1, 0'
    'cacop 0, $r1, 2048'
    'csrrd $r1, 16384'
    'csrwr $r1, -1'
    'csrxchg $r1, $r0, 0'
    'csrxchg $r1, $r1, 0'
    'csrxchg $r1, $r2, 16384'
    'break 32768'
    'syscall -1'
    'dbar 32768'
    'ibar -1'
    'idle 32768'
)
for bad in "${negatives[@]}"; do
    printf '.text\n%s\n' "$bad" > "$work/reject.s"
    if "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
           -o "$work/reject.o" "$work/reject.s" >/dev/null 2>&1; then
        echo "unexpected Loong special-register acceptance: $bad" >&2
        exit 1
    fi
done
if [[ "${MINIAS_LOONG_SKIP_GNU:-0}" == 1 ]]; then
    echo "MINIAS_LOONG_REGISTER_BANKS=PASS GNU_bytes=NOT_TESTED positive=44 rejected=${#negatives[@]}"
else
    echo "MINIAS_LOONG_REGISTER_BANKS=PASS GNU_bytes=exact positive=44 rejected=${#negatives[@]}"
fi
