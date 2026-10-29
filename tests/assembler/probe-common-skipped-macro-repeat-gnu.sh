#!/usr/bin/env bash
# Macro/repeat collectors are lexical, but disabled .if regions must not
# register macros, evaluate discarded repetition counts or emit labels.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
for tool in as objcopy cmp; do
    command -v "$tool" >/dev/null || { echo "missing GNU tool: $tool" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/conditional.s" <<'ASM'
.text
.if 0
.endm
.endr
.macro hidden_macro
    .byte 0xee
.endm
.rept unavailable_repeat_count
    .byte 0xef
.endr
.irp item,first,second
    .byte 0xf0
.endr
.endif
.if 0
.macro lexically_skipped_unclosed
.endif
.macro hidden_macro
    .byte 0x31
.endm
hidden_macro
.if 1
    .rept 2
        .byte 0x42
    .endr
    .irp item,0x53,0x64
        .byte \item
    .endr
.endif
.byte 0x75
ASM
as --64 -o "$work/gnu.o" "$work/conditional.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/conditional.s"
objcopy -O binary -j .text "$work/gnu.o" "$work/gnu.bin"
objcopy -O binary -j .text "$work/mini.o" "$work/mini.bin"
cmp "$work/gnu.bin" "$work/mini.bin"
echo 'MINIAS_SKIPPED_MACRO_REPEAT=PASS GNU_text=exact inactive_macro=not_defined inactive_repeat=not_evaluated active_nested=correct'
