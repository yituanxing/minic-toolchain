#!/usr/bin/env bash
# AArch64 GAS keeps `directive (expression)` together when binding a comma-
# delimited macro argument. x86 GAS splits this syntax: this is target policy.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in aarch64-linux-gnu-as aarch64-linux-gnu-objcopy aarch64-linux-gnu-nm cmp; do
    command -v "$tool" >/dev/null || exit 1
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/macros.s" <<'ASM'
.text
.macro alternative_insn first, second, cap, enable = 1
.if \enable
    \first
    \second
    .long \cap
.endif
.long \enable
.endm
start:
alternative_insn nop, .long (0xd500401f | ((3) << 16 | (2) << 5) | ((!!(1)) << 8)), 17
without_parens:
alternative_insn nop, .long 15, 17
with_quotes:
alternative_insn "nop", ".long (9+8)", 19, 1
end:
.data
.byte
.hword
.word
.long
.quad
.byte 0x5a
final:
ASM
aarch64-linux-gnu-as -o "$work/gnu.o" "$work/macros.s"
"$MINIAS" --target=aarch64 -o "$work/mini.o" "$work/macros.s"
for variant in gnu mini; do
    aarch64-linux-gnu-objcopy -O binary -j .text "$work/$variant.o" "$work/$variant.text.bin"
    aarch64-linux-gnu-objcopy -O binary -j .data "$work/$variant.o" "$work/$variant.data.bin"
    aarch64-linux-gnu-nm -n "$work/$variant.o" |
        awk '$3=="start" || $3=="without_parens" || $3=="with_quotes" || $3=="end" || $3=="final" {print $1,$3}' \
        >"$work/$variant.symbols"
done
cmp "$work/gnu.text.bin" "$work/mini.text.bin"
cmp "$work/gnu.data.bin" "$work/mini.data.bin"
cmp "$work/gnu.symbols" "$work/mini.symbols"
test "$(wc -c <"$work/mini.text.bin")" -eq 44
test "$(wc -c <"$work/mini.data.bin")" -eq 1
for bad in '.long (1 + )' '.hword (2 * )' '.quad unknown+'; do
    printf '.text\n%s\n' "$bad" >"$work/bad.s"
    if "$MINIAS" --target=aarch64 -o "$work/bad.o" "$work/bad.s" \
            >/dev/null 2>&1; then
        echo "MINIAS_A64_MACRO_PARENTHESIZED=FAIL accepted_invalid=$bad" >&2
        exit 1
    fi
done
echo 'MINIAS_A64_MACRO_PARENTHESIZED=PASS GNU_text=exact GNU_symbols=exact empty_directives=5 invalid_rejected=3'
