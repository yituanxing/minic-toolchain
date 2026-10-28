#!/usr/bin/env bash
# A fixed GNU .org is a persistent MCFragment position constraint.  Long
# branches can grow before it without reopening the source or moving its
# target.  Parse-time lexical directives must not force a source reparse.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
: "${ARCH:?Target required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "ORG_LAYOUT_FAIL arch=$ARCH command=$BASH_COMMAND" >&2; test ! -f "$work/trace" || cat "$work/trace" >&2' ERR

case "$ARCH" in
  x86_64)
    mini=(--target=x86_64); gas=(as --64); copy=(objcopy)
    branch='je far'; nop='nop'; short_org=2; repeats=200;;
  riscv)
    mini=(--target=riscv); gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64)
    copy=(riscv64-linux-gnu-objcopy)
    branch='beq a0,a1,far'; nop='addi x0,x0,0'; short_org=4; repeats=1200;;
  *) echo "unsupported ARCH=$ARCH" >&2; exit 2;;
esac

# The fixed target is reached after a short-to-long branch expansion, so
# the .org padding MUST shrink.  Lexical conditions/macros/assignments are
# deliberately mixed in to reject accidental whole-source reparsing.
cat >"$work/absolute.s" <<SRC
.text
.globl entry
.equ literal_equ, 0x11
.set literal_set, 0x22
.altmacro
.noaltmacro
.macro emit_nop
  $nop
.endm
entry:
.if 1
  emit_nop
.endif
.ifc alpha,alpha
  emit_nop
.endif
.ifdef literal_set
  emit_nop
.endif
.ifeq 0
  emit_nop
.endif
.purgem emit_nop
  $branch
  .org 5000
far:
  $nop
.data
  .long literal_equ,literal_set
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
  -o "$work/mini.o" "$work/absolute.s" 2>"$work/trace"
"${gas[@]}" -o "$work/gnu.o" "$work/absolute.s"
grep -q 'minias-fragment-relayout:inplace-passes=' "$work/trace"
grep -q 'minias-fragment-graph:anchors_built=1.*reparse=0' "$work/trace"
for sec in .text .data; do
  "${copy[@]}" -j "$sec" -O binary "$work/mini.o" "$work/mini.${sec##.}"
  "${copy[@]}" -j "$sec" -O binary "$work/gnu.o" "$work/gnu.${sec##.}"
  cmp "$work/mini.${sec##.}" "$work/gnu.${sec##.}"
done

# When expansion would cross a frozen .org target, reject it.  Silently
# overlapping or truncating the Section is never a valid relaxation.
cat >"$work/backwards.s" <<SRC
.text
entry:
  $branch
  .org $short_org
  .rept $repeats
    $nop
  .endr
far:
  $nop
SRC
if "$MINIAS" "${mini[@]}" -o "$work/backwards.o" \
   "$work/backwards.s" 2>"$work/backwards.err"; then
  echo 'MiniAS accepted backwards .org after branch expansion' >&2
  exit 1
fi
grep -q 'org-backwards-after-layout' "$work/backwards.err"
test ! -e "$work/backwards.o"

echo "MINIAS_MC_ORG_LEXICAL_LAYOUT=PASS arch=$ARCH persistent_org=1 lexical_parse=1 GNU_exact=1 backwards_rejected=1"
