#!/usr/bin/env bash
# GNU macro whitespace fallback may split `.long (EXPR)` when an optional
# fourth parameter is omitted. The resulting empty `.long` must emit no bytes.
# Explicitly quoted operands remain indivisible. Compare macro expansion,
# fixed-width empty directives and symbol offsets byte-for-byte with GNU AS.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in as objcopy nm cmp; do
    command -v "$tool" >/dev/null || exit 1
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/macros.s" <<'ASM'
.text
.macro optional_instruction first, second, cap, enable = 1
.if \enable
    \first
    \second
    .byte \cap
.endif
.byte \enable
.endm
before:
optional_instruction nop, .long (0x00100040 | ((3) << 16)), 17
optional_instruction "nop", ".long (0x2000 + (3 << 4))", 18, 1
after:
.byte
.short
.word
.long
.quad
.byte 0x5a
final:
ASM
as --64 -o "$work/gnu.o" "$work/macros.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/macros.s"
for variant in gnu mini; do
    objcopy -O binary -j .text "$work/$variant.o" "$work/$variant.bin"
    nm -n "$work/$variant.o" |
        awk '$3=="before" || $3=="after" || $3=="final" {print $1,$3}' \
        >"$work/$variant.symbols"
done
cmp "$work/gnu.bin" "$work/mini.bin"
cmp "$work/gnu.symbols" "$work/mini.symbols"
test "$(wc -c <"$work/mini.bin")" -eq 11

for bad in '.long (1 + )' '.short (2 * )' '.quad unknown+'; do
    printf '.text\n%s\n' "$bad" >"$work/bad.s"
    if "$MINIAS" --target=x86_64 -o "$work/bad.o" "$work/bad.s" \
            >/dev/null 2>&1; then
        echo "MINIAS_MACRO_OPTIONAL_DATA=FAIL accepted_invalid=$bad" >&2
        exit 1
    fi
done
echo 'MINIAS_MACRO_OPTIONAL_DATA=PASS GNU_text=exact symbols=exact empty_directives=5 invalid_rejected=3'
