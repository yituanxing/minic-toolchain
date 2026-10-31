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
    .org 128, 0xc3
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
grep -Eq '^minias-mcexpr-variable:typed=[1-9][0-9]* fallback=0$' "$work/trace"
for sec in .data .rodata; do
  "${copy[@]}" -j "$sec" -O binary "$work/mini.o" "$work/mini.${sec##.}"
  "${copy[@]}" -j "$sec" -O binary "$work/gnu.o" "$work/gnu.${sec##.}"
  cmp "$work/mini.${sec##.}" "$work/gnu.${sec##.}"
done
test "$(wc -c <"$work/mini.data")" -eq 129

# GNU .org targets the entire section, not the active numbered subsection.
# A preceding subsection may grow in a deferred forward-symbol data pass;
# MCLayout must then shrink the later subsection's .org in the SAME fixed
# point.  The two subsection fragments concatenate in numeric order.
cat >"$work/subsection.s" <<'SRC'
.data
.subsection 1
.globl subsection_origin
.org 32, 0x76
subsection_origin:
  .byte 0xbb
.subsection 0
  .space span_end-span_begin,0x5a
  .byte 0xaa
.section .rodata,"a"
span_begin:
  .byte 1,2,3,4,5,6,7,8
span_end:
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
  -o "$work/sub-mini.o" "$work/subsection.s" 2>"$work/trace"
"${gas[@]}" -o "$work/sub-gnu.o" "$work/subsection.s"
grep -q 'minias-fragment-graph:anchors_built=1.*reparse=0' "$work/trace"
grep -Eq '^minias-mcexpr-variable:typed=[1-9][0-9]* fallback=0$' "$work/trace"
for sec in .data .rodata; do
  "${copy[@]}" -j "$sec" -O binary "$work/sub-mini.o" "$work/sub-mini.${sec##.}"
  "${copy[@]}" -j "$sec" -O binary "$work/sub-gnu.o" "$work/sub-gnu.${sec##.}"
  cmp "$work/sub-mini.${sec##.}" "$work/sub-gnu.${sec##.}"
done
test "$(wc -c <"$work/sub-mini.data")" -eq 33
mini_origin=$(readelf -sW "$work/sub-mini.o" | awk '$NF == "subsection_origin" {print $2}')
gnu_origin=$(readelf -sW "$work/sub-gnu.o" | awk '$NF == "subsection_origin" {print $2}')
test "$mini_origin" = "$gnu_origin" && test "$mini_origin" = '0000000000000020'

# A retained *compound* .org expression crosses two numbered subsections.
# The anchor's owner subsection has a nonzero base, so evaluating its raw
# subsection-local value (rather than final Section-relative SymbolID) is
# incorrect even when .org's own output subsection remains stable.
cat >"$work/compound-subsection.s" <<'SRC'
.data
.subsection 0
  .space span_end-span_begin,0x61
  .byte 0xaa
.subsection 1
compound_anchor:
  .byte 0xbb
.subsection 2
  .org compound_anchor + 32 + (compound_anchor - compound_anchor), 0x72
.globl compound_origin
compound_origin:
  .byte 0xcc
.section .rodata,"a"
span_begin:
  .byte 1,2,3,4,5,6,7,8
span_end:
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
  -o "$work/compound-mini.o" "$work/compound-subsection.s" 2>"$work/trace"
"${gas[@]}" -o "$work/compound-gnu.o" "$work/compound-subsection.s"
grep -q 'minias-fragment-graph:anchors_built=1.*reparse=0' "$work/trace"
for sec in .data .rodata; do
  "${copy[@]}" -j "$sec" -O binary "$work/compound-mini.o" "$work/compound-mini.${sec##.}"
  "${copy[@]}" -j "$sec" -O binary "$work/compound-gnu.o" "$work/compound-gnu.${sec##.}"
  cmp "$work/compound-mini.${sec##.}" "$work/compound-gnu.${sec##.}"
done
mini_origin=$(readelf -sW "$work/compound-mini.o" | awk '$NF == "compound_origin" {print $2}')
gnu_origin=$(readelf -sW "$work/compound-gnu.o" | awk '$NF == "compound_origin" {print $2}')
test "$mini_origin" = "$gnu_origin" && test "$mini_origin" = '0000000000000029'

# Deferred `.org` is not license to emit a bogus zero target: undefined
# SymbolIDs and cross-Section arithmetic must fail closed after parsing.
cat >"$work/undefined-org.s" <<'SRC'
.text
  .org missing+8+(missing-missing),0x42
  .byte 1
SRC
if "$MINIAS" "${mini[@]}" -o "$work/undefined-org.o" \
  "$work/undefined-org.s" 2>"$work/undefined-org.err"; then
  echo 'MiniAS accepted unresolved typed .org' >&2
  exit 1
fi
grep -q 'org-expression-unresolved:' "$work/undefined-org.err"
test ! -e "$work/undefined-org.o"
cat >"$work/cross-section-org.s" <<'SRC'
.section .rodata,"a"
foreign_anchor:
  .byte 0
.data
  .org foreign_anchor+32+(foreign_anchor-foreign_anchor),0x23
  .byte 1
SRC
if "$MINIAS" "${mini[@]}" -o "$work/cross-section-org.o" \
  "$work/cross-section-org.s" 2>"$work/cross-section-org.err"; then
  echo 'MiniAS accepted cross-Section typed .org target' >&2
  exit 1
fi
grep -Eq 'org-expression-unresolved:|unsupported-expression:.org:' \
  "$work/cross-section-org.err"
test ! -e "$work/cross-section-org.o"

# The target of a .org in subsection 1 may itself be a GCC .set anchor
# from subsection 0, with a late DATA fragment changing both addresses.
cat >"$work/cross-subsection.s" <<'SRC'
.data
.subsection 0
.set subsection_anchor, . + 4
  .space span_end-span_begin,0x5a
  .byte 0xaa
.subsection 1
.org subsection_anchor + 28, 0x93
.globl cross_origin
cross_origin:
  .byte 0xbb
.section .rodata,"a"
span_begin:
  .byte 1,2,3,4,5,6,7,8
span_end:
SRC
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" "${mini[@]}" \
  -o "$work/cross-mini.o" "$work/cross-subsection.s" 2>"$work/trace"
"${gas[@]}" -o "$work/cross-gnu.o" "$work/cross-subsection.s"
grep -q 'minias-fragment-graph:anchors_built=1.*reparse=0' "$work/trace"
grep -Eq '^minias-mcexpr-variable:typed=[1-9][0-9]* fallback=0$' "$work/trace"
for sec in .data .rodata; do
  "${copy[@]}" -j "$sec" -O binary "$work/cross-mini.o" "$work/cross-mini.${sec##.}"
  "${copy[@]}" -j "$sec" -O binary "$work/cross-gnu.o" "$work/cross-gnu.${sec##.}"
  cmp "$work/cross-mini.${sec##.}" "$work/cross-gnu.${sec##.}"
done
mini_origin=$(readelf -sW "$work/cross-mini.o" | awk '$NF == "cross_origin" {print $2}')
gnu_origin=$(readelf -sW "$work/cross-gnu.o" | awk '$NF == "cross_origin" {print $2}')
test "$mini_origin" = "$gnu_origin" && test "$mini_origin" = '0000000000000020'

echo "MINIAS_MC_ORG_DATA_FIXEDPOINT=PASS arch=$ARCH forward_size=8 fixed_target=128 subsection_origin=32 anchored_cross_subsection=1 typed_cross_subsection=1 fill=4 GNU_exact=1 negative_undefined=1 negative_cross_section=1 source_reparse=0"
