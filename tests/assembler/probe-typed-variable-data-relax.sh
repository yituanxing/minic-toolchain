#!/usr/bin/env bash
# GNU cross-check: long conditional branch forces relayout of forward DATA
# fragment with a dot-relative symbolic size; output uses frozen final
# typed .space / .fill values with no emission-time source parse.
# Keep the GNU RV64 branch target 2-byte aligned: .data size is 12 bytes.
set -Eeuo pipefail
: "${MINIAS:?MiniAS required}"
: "${ARCH:?Architecture missing}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "VARIABLE_RELAX_FAIL arch=$ARCH command=$BASH_COMMAND" >&2; test ! -f "$work/trace" || cat "$work/trace" >&2' ERR
case "$ARCH" in
x86_64)
  mini=(--target=x86_64); gas=(as --64); copy=(objcopy)
  branch='je far'; nop='nop';;
riscv)
  mini=(--target=riscv)
  gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
  copy=(riscv64-linux-gnu-objcopy)
  branch='beq a0,a1,far'; nop='addi x0,x0,0';;
*) exit 2;;
esac
cat >"$work/mc.s" <<SRC
.text
.globl entry
entry:
    $branch
    .space gap_end-gap_start,0
    .rept 1200
      $nop
    .endr
far:
    $nop
.data
gap_start:
    .fill 3,2,0x1234
    .skip 6,0x67
gap_end:
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" -o "$work/mini.o" "$work/mc.s" 2>"$work/trace"
"${gas[@]}" -o "$work/gnu.o" "$work/mc.s"
grep -q 'minias-fragment-relayout:inplace-passes=' "$work/trace"
grep -q 'minias-fragment-graph:anchors_built=1' "$work/trace"
for sec in .text .data; do
    "${copy[@]}" -j "$sec" -O binary "$work/mini.o" "$work/mini.${sec##.}"
    "${copy[@]}" -j "$sec" -O binary "$work/gnu.o" "$work/gnu.${sec##.}"
    cmp "$work/mini.${sec##.}" "$work/gnu.${sec##.}"
done
echo "MINIAS_TYPED_VARIABLE_DATA=PASS arch=$ARCH relayout=GNU_exact"
