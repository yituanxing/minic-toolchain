#!/usr/bin/env bash
# .if/.elseif must use Common MCExpr operator precedence, while retaining
# the pre-existing MiniAS-only single '=' condition spelling for old sources.
set -Eeuo pipefail
: "$MINIAS"
for tool in as objcopy cmp od tr; do command -v "$tool" >/dev/null; done
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT
cat >"$w/expressions.s" <<'SRC'
.text
.set threshold,3
.if ((threshold+1)*2) >= 8
.byte 0x11
.endif
.if (1 << 3) == 8
.byte 0x22
.endif
.if ((5 != 4) && (2 < 3))
.byte 0x33
.endif
.if (2 > 3) || (7 <= 7)
.byte 0x44
.endif
.if (0x7fffffff >> 1) == 0x3fffffff
.byte 0x55
.endif
.if (1 == 2)
.byte 0xee
.elseif 2 > 1
.byte 0x66
.endif
.if (1 << 2) > (2 << 1)
.byte 0xff
.else
.byte 0x77
.endif
SRC
as --64 -o "$w/gnu.o" "$w/expressions.s"
"$MINIAS" --target=x86_64 -o "$w/mini.o" "$w/expressions.s"
for name in gnu mini; do
  objcopy -j .text -O binary "$w/$name.o" "$w/$name.bin"
done
cmp "$w/gnu.bin" "$w/mini.bin"
test "$(od -An -tx1 -v "$w/mini.bin" | tr -d ' \n')" = '11223344556677'

# MiniAS previously admitted this legacy spelling even though current
# GNU GAS requires '=='. Preserve existing caller behavior during cleanup.
cat >"$w/compat.s" <<'SRC'
.text
.set docfi,0
.if docfi=0
.byte 0x88
.endif
SRC
"$MINIAS" --target=x86_64 -o "$w/compat.o" "$w/compat.s"
objcopy -j .text -O binary "$w/compat.o" "$w/compat.bin"
test "$(od -An -tx1 -v "$w/compat.bin" | tr -d ' \n')" = '88'
echo 'MINIAS_IF_COMMON=PASS GNU_text=exact logical_shift_comparison=7 legacy_equal=1'
