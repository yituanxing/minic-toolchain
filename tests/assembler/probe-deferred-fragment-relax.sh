#!/usr/bin/env bash
# Forward variable-sized data fragments and branch relaxation share ONE
# persistent symbol/layout solver; no source reparse or target-local patches.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${RISCV_AS:=riscv64-linux-gnu-as}"
: "${RISCV_OBJCOPY:=riscv64-linux-gnu-objcopy}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/relax.s" <<'SRC'
.text
.globl entry
entry:
    beq a0,a1,far
    .space gap_end-gap_begin,0
    .balign 16
    .rept 1200
       nop
    .endr
far:
    ret
.data
gap_begin:
    .byte 1,2,3,4,5,6,7
gap_end:
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" -march=rv64i -mabi=lp64 \
    -o "$work/mini.o" "$work/relax.s" 2>"$work/trace"
"$RISCV_AS" -march=rv64i -mabi=lp64 \
    -o "$work/gnu.o" "$work/relax.s"
grep -q 'minias-fragment-relayout:inplace-passes=' "$work/trace"
"$RISCV_OBJCOPY" -j .text -O binary "$work/mini.o" "$work/mini.text"
"$RISCV_OBJCOPY" -j .text -O binary "$work/gnu.o" "$work/gnu.text"
"$RISCV_OBJCOPY" -j .data -O binary "$work/mini.o" "$work/mini.data"
"$RISCV_OBJCOPY" -j .data -O binary "$work/gnu.o" "$work/gnu.data"
cmp "$work/mini.text" "$work/gnu.text"
cmp "$work/mini.data" "$work/gnu.data"
echo "MINIAS_DEFERRED_FRAGMENT_RELAX=PASS rv64i=GNU_exact source_reparse=0 alignment=recomputed"
