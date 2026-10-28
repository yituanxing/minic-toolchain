#!/usr/bin/env bash
# GCC emits .size against a function's current dot. The end of the function
# is an MCFragment anchor, optionally with a signed constant displacement;
# branch relaxation must move it, not freeze the parse-site symbol size.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${ARCH:?Target required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "SIZE_ANCHOR_FAIL arch=$ARCH command=$BASH_COMMAND" >&2; test ! -f "$work/trace" || cat "$work/trace" >&2' ERR
case "$ARCH" in
  x86_64) mini=(--target=x86_64); gas=(as --64); copy=(objcopy)
    branch='je far'; nop='nop'; repeats=1200;;
  riscv) mini=(--target=riscv); gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
    copy=(riscv64-linux-gnu-objcopy)
    branch='beq a0,a1,far'; nop='addi x0,x0,0'; repeats=1200;;
  aarch64) mini=(--target=aarch64); gas=(aarch64-linux-gnu-as)
    copy=(aarch64-linux-gnu-objcopy);;
  loongarch64) mini=(--target=loongarch64); gas=(loongarch64-linux-gnu-as)
    copy=(loongarch64-linux-gnu-objcopy);;
  *) echo "unsupported ARCH=$ARCH" >&2;exit 2;;
esac

cat >"$work/size.s" <<'SRC'
.data
.globl addend_object
.type addend_object,%object
addend_object:
    .space span_end-span_begin,0x5a
    .byte 0xaa
.size addend_object, . - addend_object + 4
.globl parenthesized_object
.type parenthesized_object,%object
parenthesized_object:
    .byte 0xbb,0xcc,0xdd,0xee
.size parenthesized_object, (.-parenthesized_object)
.globl subtract_object
.type subtract_object,%object
subtract_object:
    .byte 0x11,0x22,0x33
.size subtract_object, .-(subtract_object+2)
.section .rodata,"a"
span_begin: .byte 1,2,3,4,5,6,7,8
span_end:
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
  -o "$work/mini.o" "$work/size.s" 2>"$work/trace"
"${gas[@]}" -o "$work/gnu.o" "$work/size.s"
grep -q '^minias-fragment-graph:anchors_built=1.*reparse=0$' "$work/trace"
grep -Eq '^minias-mcexpr-variable:typed=[1-9][0-9]* fallback=0$' "$work/trace"
for sec in .data .rodata; do
  "${copy[@]}" -j "$sec" -O binary "$work/mini.o" "$work/mini.${sec##.}"
  "${copy[@]}" -j "$sec" -O binary "$work/gnu.o" "$work/gnu.${sec##.}"
  cmp "$work/mini.${sec##.}" "$work/gnu.${sec##.}"
done
for sym in addend_object parenthesized_object subtract_object; do
  mini_size=$(readelf -sW "$work/mini.o" | awk -v n="$sym" '$NF == n {print $3}')
  gnu_size=$(readelf -sW "$work/gnu.o" | awk -v n="$sym" '$NF == n {print $3}')
  test -n "$mini_size" && test "$mini_size" = "$gnu_size"
done

# The x86 and RISC-V Targets must grow a real machine branch inside a
# declared function, then re-evaluate its Common .size end anchor in place.
if [[ -v branch ]]; then
  cat >"$work/branch.s" <<SRC
.text
.globl growing_function
.type growing_function,%function
growing_function:
  $branch
  .rept $repeats
    $nop
  .endr
far:
  $nop
.size growing_function, .-growing_function+3
SRC
  MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
    -o "$work/branch-mini.o" "$work/branch.s" 2>"$work/trace"
  "${gas[@]}" -o "$work/branch-gnu.o" "$work/branch.s"
  grep -q 'minias-fragment-relayout:inplace-passes=' "$work/trace"
  grep -q '^minias-fragment-graph:anchors_built=1.*reparse=0$' "$work/trace"
  "${copy[@]}" -j .text -O binary "$work/branch-mini.o" "$work/branch-mini.bin"
  "${copy[@]}" -j .text -O binary "$work/branch-gnu.o" "$work/branch-gnu.bin"
  cmp "$work/branch-mini.bin" "$work/branch-gnu.bin"
  mini_size=$(readelf -sW "$work/branch-mini.o" | awk '$NF=="growing_function" {print $3}')
  gnu_size=$(readelf -sW "$work/branch-gnu.o" | awk '$NF=="growing_function" {print $3}')
  test -n "$mini_size" && test "$mini_size" = "$gnu_size"
fi
echo "MINIAS_SIZE_END_ANCHOR=PASS arch=$ARCH GNU_symbols=exact signed_addend=1 forward_data=1 branch_relayout=verified_if_supported"
