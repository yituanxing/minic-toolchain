#!/usr/bin/env bash
# GNU LoongArch is the authority for target-local LSX/LASX operand classes,
# bounds and machine bytes. The same test deliberately mixes both extensions.
set -Eeuo pipefail
trap 'echo "MINIAS_LOONG_VECTOR_FORMAT_FAIL line=$LINENO command=$BASH_COMMAND" >&2' ERR
: "${MINIAS:?}"
: "${LOONGAS:?}"
: "${LOONGCOPY:?}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/lsx-lasx.s" <<'ASM'
.text
.globl loong_vector_formats
loong_vector_formats:
    vld $vr0,$r4,-2048
    vst $vr31,$r20,2047
    xvld $xr31,$r2,-2048
    xvst $xr0,$r31,2047
    vand.v $vr0,$vr1,$vr31
    vor.v $vr31,$vr0,$vr2
    vxor.v $vr5,$vr6,$vr7
    vnor.v $vr7,$vr6,$vr5
    vandn.v $vr0,$vr2,$vr4
    vorn.v $vr31,$vr0,$vr1
    xvand.v $xr0,$xr1,$xr31
    xvor.v $xr31,$xr0,$xr2
    xvxor.v $xr5,$xr6,$xr7
    xvnor.v $xr7,$xr6,$xr5
    xvorn.v $xr31,$xr0,$xr1
    vandi.b $vr0,$vr31,255
    vnori.b $vr31,$vr0,0
    xvandi.b $xr0,$xr31,255
    xvnori.b $xr31,$xr0,0
    vslti.b $vr1,$vr2,-16
    vslti.d $vr31,$vr0,15
    xvslti.b $xr1,$xr2,-16
    xvslti.d $xr31,$xr0,15
    vslli.b $vr1,$vr2,7
    vslli.h $vr1,$vr2,15
    vsrli.w $vr1,$vr2,31
    vsrai.d $vr1,$vr2,63
    xvslli.b $xr1,$xr2,7
    xvslli.h $xr1,$xr2,15
    xvsrli.w $xr1,$xr2,31
    xvsrli.d $xr1,$xr2,63
    vpickve2gr.b $r1,$vr31,15
    vpickve2gr.hu $r4,$vr10,7
    vpickve2gr.wu $r4,$vr10,3
    vpickve2gr.du $r4,$vr10,1
    xvpickve2gr.w $r1,$xr31,7
    xvpickve2gr.du $r4,$xr10,3
    vinsgr2vr.b $vr31,$r4,15
    vinsgr2vr.d $vr31,$r4,1
    xvinsgr2vr.w $xr31,$r4,7
    xvinsgr2vr.d $xr31,$r4,3
    xvpermi.w $xr31,$xr0,255
    xvpermi.d $xr0,$xr31,0
    xvpermi.q $xr31,$xr0,255
    xvreplve0.b $xr0,$xr31
    xvreplve0.q $xr31,$xr0
    vshuf.b $vr0,$vr1,$vr2,$vr31
    vbitsel.v $vr31,$vr2,$vr1,$vr0
    xvshuf.b $xr0,$xr1,$xr2,$xr31
    xvbitsel.v $xr31,$xr2,$xr1,$xr0
ASM

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/mini.o" "$work/lsx-lasx.s"
if [[ "${MINIAS_LOONG_SKIP_GNU:-0}" != 1 ]]; then
    "$LOONGAS" -mabi=lp64d -o "$work/gnu.o" "$work/lsx-lasx.s"
    "$LOONGCOPY" -j .text -O binary "$work/mini.o" "$work/mini.bin"
    "$LOONGCOPY" -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
    cmp "$work/mini.bin" "$work/gnu.bin"
fi

negatives=(
    'vld $vr0,$xr0,0'
    'xvld $vr0,$r1,0'
    'vst $vr0,$r1,2048'
    'xvst $xr0,$r1,-2049'
    'vand.v $vr0,$vr1,$xr1'
    'xvand.v $xr0,$xr1,$vr1'
    'vandi.b $vr0,$vr1,-1'
    'xvandi.b $xr0,$xr1,256'
    'vslti.b $vr0,$vr1,-17'
    'xvslti.b $xr0,$xr1,16'
    'vslli.b $vr0,$vr1,8'
    'xvslli.d $xr0,$xr1,64'
    'vpickve2gr.d $r0,$vr0,2'
    'xvpickve2gr.w $r0,$xr0,8'
    'vinsgr2vr.h $vr0,$r0,8'
    'xvinsgr2vr.d $xr0,$r0,4'
    'xvpermi.q $xr0,$xr1,256'
    'xvreplve0.q $xr0,$vr1'
    'vshuf.b $vr0,$vr1,$vr2,$xr3'
    'xvshuf.b $xr0,$xr1,$xr2,$vr3'
)
for i in "${!negatives[@]}"; do
    printf '.text\n%s\n' "${negatives[i]}" > "$work/bad.s"
    if "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
          -o "$work/bad.o" "$work/bad.s" >/dev/null 2>&1; then
        echo "unexpected Loong vector acceptance: ${negatives[i]}" >&2
        exit 1
    fi
done
if [[ "${MINIAS_LOONG_SKIP_GNU:-0}" == 1 ]]; then
    echo "MINIAS_LOONG_VECTOR_TYPED_PATTERNS=PASS GNU_bytes=NOT_TESTED positive=48 rejected=${#negatives[@]}"
else
    echo "MINIAS_LOONG_VECTOR_TYPED_PATTERNS=PASS GNU_bytes=exact positive=48 rejected=${#negatives[@]}"
fi
