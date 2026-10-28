#!/usr/bin/env bash
# Forward variable-sized data fragments and branch relaxation share ONE
# persistent symbol/layout solver; no source reparse or target-local patches.
set -Eeuo pipefail
trap 'echo "DEFERRED_RELAX_FAIL line=$LINENO command=$BASH_COMMAND" >&2; if test -n "${work:-}" && test -f "$work/trace"; then cat "$work/trace" >&2; fi' ERR
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
# An explicit RV64I policy deliberately rejects raw .space in .text;
# this layout-oracle uses the permissive base-I target profile instead.
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=riscv \
    -o "$work/mini.o" "$work/relax.s" 2>"$work/trace"
"$RISCV_AS" -march=rv64i -mabi=lp64 \
    -o "$work/gnu.o" "$work/relax.s"
grep -q 'minias-fragment-relayout:inplace-passes=' "$work/trace"
"$RISCV_OBJCOPY" -j .text -O binary "$work/mini.o" "$work/mini.text"
"$RISCV_OBJCOPY" -j .text -O binary "$work/gnu.o" "$work/gnu.text"
"$RISCV_OBJCOPY" -j .data -O binary "$work/mini.o" "$work/mini.data"
"$RISCV_OBJCOPY" -j .data -O binary "$work/gnu.o" "$work/gnu.data"
if ! cmp "$work/mini.text" "$work/gnu.text"; then
    echo "MINIAS_DEFERRED_DEBUG mini+GNU symbols" >&2
    readelf -Ws "$work/mini.o" | grep -E '(far|entry|gap_)' >&2 || true
    readelf -Ws "$work/gnu.o" | grep -E '(far|entry|gap_)' >&2 || true
    echo "MINIAS_DEFERRED_DEBUG MiniAS bytes" >&2
    od -An -tx1 -N48 "$work/mini.text" >&2
    echo "MINIAS_DEFERRED_DEBUG GNU bytes" >&2
    od -An -tx1 -N48 "$work/gnu.text" >&2
    exit 1
fi
cmp "$work/mini.data" "$work/gnu.data"
echo "MINIAS_DEFERRED_FRAGMENT_RELAX=PASS rv64i=GNU_exact source_reparse=0 alignment=recomputed"
