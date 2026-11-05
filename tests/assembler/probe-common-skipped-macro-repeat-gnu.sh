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
repeat_label: .rep 2
    .byte 0x76
.endr
ASM
as --64 -o "$work/gnu.o" "$work/conditional.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/conditional.s"
objcopy -O binary -j .text "$work/gnu.o" "$work/gnu.bin"
objcopy -O binary -j .text "$work/mini.o" "$work/mini.bin"
cmp "$work/gnu.bin" "$work/mini.bin"

# The collector is Common-owned, so exercise the same source without
# target instruction encoders on the other three object profiles as well.
for target in riscv aarch64 loongarch64; do
    case "$target" in
        riscv) opts=(--target=riscv -march=rv64i -mabi=lp64);;
        aarch64) opts=(--target=aarch64);;
        loongarch64) opts=(--target=loongarch64);;
    esac
    "$MINIAS" "${opts[@]}" -o "$work/$target.o" "$work/conditional.s"
    test -s "$work/$target.o"
done

# These lengths were a scratch-buffer implementation cap, not GNU
# grammar limits. Both inputs are valid in GNU GAS (checked independently).
# A Source collector must not reject them before semantic evaluation.
macro_arg=$(printf '%01024d' 0)
repeat_arg=$(printf '%0256d' 0)
printf '.macro x%s\n.endm\n' "$macro_arg" >"$work/long-macro.s"
printf '.rept %s\n.endr\n' "$repeat_arg" >"$work/long-repeat.s"
for kind in macro repeat; do
    as --64 -o "$work/long-$kind.gnu.o" "$work/long-$kind.s"
    "$MINIAS" --target=x86_64 -o "$work/long-$kind.o" \
        "$work/long-$kind.s"
    test -s "$work/long-$kind.gnu.o"
    test -s "$work/long-$kind.o"
done
echo 'MINIAS_SKIPPED_MACRO_REPEAT=PASS GNU_text=exact inactive_macro=not_defined inactive_repeat=not_evaluated active_nested=correct label_rep=exact four_targets=assembled macro_repeat_bounds=GNU_accepted'
