#!/usr/bin/env bash
# Regression: AArch64 Linux proc.S expands mov_q's multiword-constant macro
# into a .if expression with more than 512 typed AST nodes. Keep GNU GAS as
# the byte oracle and also exercise the shared x86_64 expression consumers.
set -Eeuo pipefail
: "${MINIAS:?}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Flat precedence, not deep parentheses: the Linux failure was node count,
# not recursive nesting. More than 512 nodes flow through each evaluator.
wide=0x1200000000000000
for ((i=0; i<180; ++i)); do
    wide+=' | (0 << 0)'
done

cat >"$work/large-x86.s" <<'ASM'
.macro check_long value
.if (((\value) >> 31) == 0 || ((\value) >> 31) == 0x1ffffffff)
    .byte 0x11
.else
    .byte 0x22
.endif
.endm
.text
ASM
printf 'check_long %s\n.quad %s\n' "$wide" "$wide" >>"$work/large-x86.s"
# Large affine relocation and deferred same-section difference must accept
# the same shared syntax limit as the macro and immediate evaluators.
zero_chain=0
for ((i=0; i<300; ++i)); do
    zero_chain+=' + 0'
done
cat >>"$work/large-x86.s" <<'ASM'
.data
first: .byte 0x11
second: .byte 0x22
ASM
printf '.space ((first-second) >> 63) + %s + 1, 0x7e\n' "$zero_chain" >>"$work/large-x86.s"
printf '.quad external_symbol + %s\n' "$zero_chain" >>"$work/large-x86.s"
"$MINIAS" --target=x86_64 -o "$work/x86-mini.o" "$work/large-x86.s"
as --64 -o "$work/x86-gnu.o" "$work/large-x86.s"
for flavor in mini gnu; do
    objcopy -j .text -O binary "$work/x86-$flavor.o" "$work/x86-$flavor.bin"
    objcopy -j .data -O binary "$work/x86-$flavor.o" "$work/x86-$flavor.data"
done
cmp "$work/x86-mini.bin" "$work/x86-gnu.bin"
cmp "$work/x86-mini.data" "$work/x86-gnu.data"
# One unresolved absolute 64-bit relocation, identical in GNU and MiniAS.
for flavor in mini gnu; do
    readelf -Wr "$work/x86-$flavor.o" | grep 'R_X86_64_64' |
        sed -E 's/.*(R_X86_64_64)[[:space:]]+[^[:space:]]+[[:space:]]+(external_symbol).*/\1 \2/' \
        >"$work/x86-$flavor.reloc"
done
cmp "$work/x86-mini.reloc" "$work/x86-gnu.reloc"
test "$(wc -l <"$work/x86-mini.reloc")" -eq 1

# A64 checks the second evaluation path: :abs_g3: must recover the same
# full 64-bit constant after the .if macro condition has been evaluated.
if command -v aarch64-linux-gnu-as >/dev/null 2>&1 &&
   command -v aarch64-linux-gnu-objcopy >/dev/null 2>&1; then
    cat >"$work/large-a64.s" <<'ASM'
.macro mov_q reg, value
.if (((\value) >> 31) == 0 || ((\value) >> 31) == 0x1ffffffff)
    movz \reg, :abs_g1_s:\value
.else
    movz \reg, :abs_g3:\value
.endif
.endm
.text
ASM
    printf 'mov_q x0, %s\n.quad %s\n' "$wide" "$wide" >>"$work/large-a64.s"
    "$MINIAS" --target=aarch64 -o "$work/a64-mini.o" "$work/large-a64.s"
    aarch64-linux-gnu-as -o "$work/a64-gnu.o" "$work/large-a64.s"
    for flavor in mini gnu; do
        aarch64-linux-gnu-objcopy -j .text -O binary \
            "$work/a64-$flavor.o" "$work/a64-$flavor.bin"
    done
    cmp "$work/a64-mini.bin" "$work/a64-gnu.bin"
    echo 'MINIAS_LARGE_EXPRESSION_GNU=PASS x86_64=exact aarch64=exact'
else
    echo 'MINIAS_LARGE_EXPRESSION_GNU=PASS x86_64=exact aarch64=skip-no-gnu-as'
fi
