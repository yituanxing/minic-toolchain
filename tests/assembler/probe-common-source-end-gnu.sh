#!/usr/bin/env bash
# `.end` is a Common source-graph terminator even inside macro and repetition
# expansion. It must never remain a no-op that accepts unreachable source.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"

work=$(mktemp -d)
trap 'status=$?; if ((status != 0)); then echo "MINIAS_COMMON_SOURCE_END=FAIL status=$status" >&2; fi; rm -rf "$work"' EXIT

check_case() {
    local name="$1"
    as --64 -o "$work/$name-gnu.o" "$work/$name.s"
    "$MINIAS" --target=x86_64 -o "$work/$name-mini.o" "$work/$name.s"
    objcopy -j .text -O binary "$work/$name-gnu.o" "$work/$name-gnu.bin"
    objcopy -j .text -O binary "$work/$name-mini.o" "$work/$name-mini.bin"
    cmp "$work/$name-gnu.bin" "$work/$name-mini.bin"
}

cat >"$work/repeat.s" <<'SRC'
.text
.byte 0x11
.rept 3
  .byte 0x22
  .end
  .byte 0x33
.endr
.byte 0x44
SRC
check_case repeat

cat >"$work/semicolon.s" <<'SRC'
.text
.byte 0x55; .end unused_argument; .byte 0x66
.byte 0x77
SRC
check_case semicolon

cat >"$work/macro.s" <<'SRC'
.macro close_assembly
.byte 0x88
.end
.byte 0x99
.endm
.text
close_assembly
.byte 0xaa
SRC
check_case macro

cat >"$work/open-conditional.s" <<'SRC'
.text
.if 1
  .byte 0x11
.end
SRC
if "$MINIAS" --target=x86_64 -o "$work/open-mini.o" \
        "$work/open-conditional.s" >"$work/open.out" 2>&1; then
    echo 'unterminated conditional silently accepted after .end' >&2
    exit 1
fi

echo 'MINIAS_COMMON_SOURCE_END=PASS GNU_text_bytes=3 unterminated_conditional_rejected=1'
