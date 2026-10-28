#!/usr/bin/env bash
# Common MCLayout must solve a fixed .org together with deferred GNU DATA
# sizes, not freeze its parse-time padding or reopen the source.  Four real
# Target profiles use exactly the same fragment semantics and GNU oracle.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${ARCH:?Target required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "ORG_DATA_FAIL arch=$ARCH command=$BASH_COMMAND" >&2; test ! -f "$work/trace" || cat "$work/trace" >&2' ERR
case "$ARCH" in
  x86_64) mini=(--target=x86_64); gas=(as --64); copy=(objcopy);;
  riscv) mini=(--target=riscv)
    gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
    copy=(riscv64-linux-gnu-objcopy);;
  aarch64) mini=(--target=aarch64); gas=(aarch64-linux-gnu-as)
    copy=(aarch64-linux-gnu-objcopy);;
  loongarch64) mini=(--target=loongarch64); gas=(loongarch64-linux-gnu-as)
    copy=(loongarch64-linux-gnu-objcopy);;
  *) echo "unsupported ARCH=$ARCH" >&2; exit 2;;
esac

cat >"$work/layout.s" <<'SRC'
.data
.globl data_start
data_start:
    .space span_end-span_begin,0x5a
    .org 128
    .byte 0xee
.section .rodata,"a"
span_begin:
    .byte 1,2,3,4,5,6,7,8
span_end:
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
  -o "$work/mini.o" "$work/layout.s" 2>"$work/trace"
"${gas[@]}" -o "$work/gnu.o" "$work/layout.s"
grep -q 'minias-fragment-graph:anchors_built=1.*reparse=0' "$work/trace"
for sec in .data .rodata; do
  "${copy[@]}" -j "$sec" -O binary "$work/mini.o" "$work/mini.${sec##.}"
  "${copy[@]}" -j "$sec" -O binary "$work/gnu.o" "$work/gnu.${sec##.}"
  cmp "$work/mini.${sec##.}" "$work/gnu.${sec##.}"
done
test "$(wc -c <"$work/mini.data")" -eq 129
echo "MINIAS_MC_ORG_DATA_FIXEDPOINT=PASS arch=$ARCH forward_size=8 fixed_target=128 GNU_exact=1 source_reparse=0"
