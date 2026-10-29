#!/usr/bin/env bash
# The LoongArch scalar opcode table owns the operand grammar, signedness,
# field placement and GNU relocation exception. Compare independently with
# GNU GAS; malformed forms must not be silently interpreted as another ISA.
set -Eeuo pipefail
trap 'echo "MINIAS_LOONG_SCALAR_SHAPES_FAIL line=$LINENO command=$BASH_COMMAND" >&2' ERR
: "${MINIAS:?}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/shapes.s" <<'ASM'
.text
.globl scalar_shapes
scalar_shapes:
    add.d $a0,$a1,$a2
    sub.w $a3,$a4,$a5
    and $a0,$a0,$a1
    sltu $a0,$a1,$a2
    maskeqz $a0,$a1,$a2
    sll.w $a0,$a1,$a2
    rotr.d $a0,$a1,$a2
    mulh.du $a0,$a1,$a2
    div.du $a0,$a1,$a2
    ldx.wu $a0,$a1,$a2
    stx.d $a0,$a1,$a2
    clo.w $a0,$a1
    revb.d $a0,$a1
    bitrev.w $a0,$a1
    rdtime.d $a0,$a1
    cpucfg $a0,$a1
    iocsrrd.w $a0,$a1
    iocsrwr.d $a0,$a1
    addi.d $a0,$a1,-2048
    addi.w $a0,$a1,2047
    slti $a0,$a1,-1
    ld.d $a0,$a1,-2048
    st.d $a0,$a1,2047
    ld.wu $a0,$a1,0
    andi $a0,$a1,4095
    ori $a0,$a1,0
    xori $a0,$a1,42
    slli.w $a0,$a1,31
    srli.d $a0,$a1,63
    srai.w $a0,$a1,0
    rotri.d $a0,$a1,63
    ll.w $a0,$sp,-32768
    sc.d $a0,$sp,32764
    ldptr.d $a0,$sp,32764
    stptr.w $a0,$sp,-4
    lu12i.w $a0,-524288
    lu32i.d $a1,524287
    pcaddi $a0,0
    pcaddu12i $a1,-2
    pcaddu18i $a0,1
    amswap.w $a0,$a1,$a2
    amadd.d $a0,$a1,$a2
    amxor_db.w $a0,$a1,$a2
    ammin.du $a0,$a1,$a2
    amswap.w $a0,$a1,$a2,0
    jirl $zero,$ra,4+4
    pcalau12i $a0,%pc_hi20(external_symbol)
    addi.d $a0,$a0,%pc_lo12(external_symbol)
ASM
"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/mini.o" "$work/shapes.s"
if [[ "${MINIAS_LOONG_SKIP_GNU:-0}" != 1 ]]; then
    : "${LOONGAS:?}"
    : "${LOONGCOPY:?}"
    "$LOONGAS" -mabi=lp64d -o "$work/gnu.o" "$work/shapes.s"
    "$LOONGCOPY" -j .text -O binary "$work/mini.o" "$work/mini.bin"
    "$LOONGCOPY" -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
    cmp "$work/mini.bin" "$work/gnu.bin"
fi
negatives=(
    'add.d $a0,$a1,2048'
    'add.d $a0,$a1,$f0'
    'clz.w $a0,$a1,$a2'
    'addi.d $a0,$a1,2048'
    'addi.w $a0,$a1,-2049'
    'andi $a0,$a1,-1'
    'ori $a0,$a1,4096'
    'slli.w $a0,$a1,32'
    'srli.d $a0,$a1,64'
    'ldptr.d $a0,$sp,-32772'
    'ldptr.d $a0,$sp,3'
    'stptr.w $a0,$sp,32768'
    'lu12i.w $a0,524288'
    'lu32i.d $a0,-524289'
    'amswap.w $a0,$a1,$a2,1'
    'amswap.w $a0,$a1,$a2,$r0'
    'jirl $zero,$ra,2'
)
for bad in "${negatives[@]}"; do
    printf '.text\n%s\n' "$bad" > "$work/bad.s"
    if "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
          -o "$work/bad.o" "$work/bad.s" >/dev/null 2>&1; then
        echo "unexpected Loong scalar acceptance: $bad" >&2
        exit 1
    fi
done
if [[ "${MINIAS_LOONG_SKIP_GNU:-0}" == 1 ]]; then
    echo "MINIAS_LOONG_SCALAR_SHAPES=PASS GNU_bytes=NOT_TESTED positive=48 rejected=${#negatives[@]}"
else
    echo "MINIAS_LOONG_SCALAR_SHAPES=PASS GNU_bytes=exact positive=48 rejected=${#negatives[@]}"
fi
