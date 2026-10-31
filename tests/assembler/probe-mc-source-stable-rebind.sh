#!/usr/bin/env bash
# Cross-replay long branches belong to stable expansion sites and parsed
# instruction shapes, never the previous pass's statement vector ordinals.
# Deliberately change .if topology during relaxation and exercise both a
# recoverable ordinal insertion and a vanished forced instruction.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
command -v objdump >/dev/null
command -v as >/dev/null
command -v objcopy >/dev/null
command -v readelf >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "MINIAS_SOURCE_REBIND=FAIL command=$BASH_COMMAND" >&2' ERR

# The first source pass has two branches and a one-byte .if arm. Once the
# first branch expands, the opposite .if arm inserts a third branch before
# the already-forced second one. A stale ordinal would attach the old branch
# decision to the new instruction. The SourceSite mapping must converge.
{
  printf '%s\n' '.text' 'entry:' 'je far'
  printf '%s\n' '.if . - entry == 2' '  .byte 0x90' '.else' '  je far' '.endif'
  printf '%s\n' 'je far' '.rept 256' '  nop' '.endr' 'far:' '  ret'
} >"$work/reordered.s"
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=x86_64 \
  -o "$work/reordered.o" "$work/reordered.s" 2>"$work/reordered.trace"
test -s "$work/reordered.o"
test "$(objdump -d "$work/reordered.o" | grep -Ec '[[:space:]]je[[:space:]]')" -eq 3
grep -q '^minias-source-reparse:pass=1$' "$work/reordered.trace"
grep -q '^minias-source-reparse:pass=2$' "$work/reordered.trace"
grep -Eq '^minias-source-graph:file-reads=1 .*site-drifts=0 conditional-changes=[1-9][0-9]*$' "$work/reordered.trace"

# A retained branch that disappears from a changed conditional region is
# not allowed to silently force whatever statement inherits its old index.
{
  printf '%s\n' '.text' 'entry:' 'je far'
  printf '%s\n' '.if . - entry == 2' '  je far' '.else' '  nop' '.endif'
  printf '%s\n' '.rept 256' '  nop' '.endr' 'far:' '  ret'
} >"$work/vanished.s"
if MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=x86_64 \
     -o "$work/vanished.o" "$work/vanished.s" 2>"$work/vanished.trace"; then
  echo 'accepted a vanished forced branch after conditional replay' >&2
  exit 1
fi
test ! -e "$work/vanished.o"
grep -q 'lost-or-ambiguous-forced-source-site:' "$work/vanished.trace"

# Replaying an ordinary macro + REPT and a mutable .set must retain distinct
# expansion identities and recompute the value without changing source shape.
{
  printf '%s\n' '.text' 'entry:' 'je far'
  printf '%s\n' '.set branch_span, . - entry'
  printf '%s\n' '.macro pad' '  nop' '.endm'
  printf '%s\n' '.rept 256' '  pad' '.endr'
  printf '%s\n' 'far:' '  ret' '.data' '.long branch_span'
} >"$work/macro.s"
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=x86_64 \
  -o "$work/macro.o" "$work/macro.s" 2>"$work/macro.trace"
grep -Eq '^minias-source-graph:file-reads=1 .*site-drifts=0 conditional-changes=0$' "$work/macro.trace"
test "$(readelf -sW "$work/macro.o" | awk '$NF=="branch_span" {print $2}')" = 0000000000000006

# Three long branches from one macro definition are three *different* sites:
# one direct invocation and two independent REPT iterations. The same
# lexical source line must never coalesce their forced identities.
{
  printf '%s\n' '.text' 'entry:' '.macro conditional_branch' '  je far' '.endm'
  printf '%s\n' 'conditional_branch' '.rept 2' '  conditional_branch' '.endr'
  printf '%s\n' '.if . - .' '  .byte 0x90' '.endif'
  printf '%s\n' '.rept 256' '  nop' '.endr' 'far:' '  ret'
} >"$work/multi-expansion.s"
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=x86_64 \
  -o "$work/multi-expansion.o" "$work/multi-expansion.s" \
  2>"$work/multi-expansion.trace"
test "$(objdump -d "$work/multi-expansion.o" | grep -Ec '[[:space:]]je[[:space:]]')" -eq 3
grep -q '^minias-source-reparse:pass=1$' "$work/multi-expansion.trace"
grep -Eq '^minias-source-graph:file-reads=1 .*site-drifts=0 conditional-changes=0$' "$work/multi-expansion.trace"

# GNU numeric labels are lowered to private generation names. An extra 1:
# in a changed conditional arm renumbers 1f, but must not make the same
# lexical instruction appear to have changed its MC shape.
{
  printf '%s\n' '.text' 'entry:' 'je far'
  printf '%s\n' '.if . - entry == 2' '1: nop' '.else' '1: nop' '1: nop' '.endif'
  printf '%s\n' 'je 1f' '.rept 256' '  nop' '.endr' '1: ret' 'far: ret'
} >"$work/numeric-shift.s"
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=x86_64 \
  -o "$work/numeric-shift.o" "$work/numeric-shift.s" \
  2>"$work/numeric-shift.trace"
test "$(objdump -d "$work/numeric-shift.o" | grep -Ec '[[:space:]]je[[:space:]]')" -eq 2
grep -Eq '^minias-source-graph:file-reads=1 .*site-drifts=0 conditional-changes=[1-9][0-9]*$' "$work/numeric-shift.trace"

# The GNU \@ macro invocation counter similarly changes when an earlier
# conditional emits an extra harmless macro. A later stable macro call can
# still name its own generated local target in the same source instruction.
{
  printf '%s\n' '.text' 'entry:' 'je finish'
  printf '%s\n' '.macro tick' ' .byte 0' '.endm'
  printf '%s\n' '.macro branch' ' je local_\@' ' .rept 256' ' nop' ' .endr' 'local_\@: ret' '.endm'
  printf '%s\n' '.if . - entry == 2' ' tick' '.else' ' tick' ' tick' '.endif'
  printf '%s\n' 'branch' 'finish: ret'
} >"$work/counter-shift.s"
MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=x86_64 \
  -o "$work/counter-shift.o" "$work/counter-shift.s" \
  2>"$work/counter-shift.trace"
test "$(objdump -d "$work/counter-shift.o" | grep -Ec '[[:space:]]je[[:space:]]')" -eq 2
grep -Eq '^minias-source-graph:file-reads=1 .*site-drifts=0 conditional-changes=[1-9][0-9]*$' "$work/counter-shift.trace"

# Stress the unified capacity policy: large fixed-width fill, nonzero space
# and zero padding must still match GNU exact bytes.  NOBITS merely grows its
# logical section size even when .space requests a nonzero fill byte.
{
  printf '%s\n' '.data' '.fill 0,4,0x11223344' '.fill 65536,4,0x11223344'
  printf '%s\n' '.space 65536,0xa5' '.zero 65536'
  printf '%s\n' '.bss' '.space 1048576,0xff'
} >"$work/fill.s"
"$MINIAS" --target=x86_64 -o "$work/fill-mini.o" "$work/fill.s"
as --64 -o "$work/fill-gnu.o" "$work/fill.s"
objcopy -j .data -O binary "$work/fill-mini.o" "$work/fill-mini.bin"
objcopy -j .data -O binary "$work/fill-gnu.o" "$work/fill-gnu.bin"
cmp "$work/fill-mini.bin" "$work/fill-gnu.bin"
test "$(wc -c <"$work/fill-mini.bin")" -eq 393216
mini_bss=$(readelf -SW "$work/fill-mini.o" | awk '$3==".bss" {print $7}')
gnu_bss=$(readelf -SW "$work/fill-gnu.o" | awk '$3==".bss" {print $7}')
test -n "$mini_bss" && test "$mini_bss" = "$gnu_bss"

echo 'MINIAS_SOURCE_REBIND=PASS topology_reordered=3 replay=2 missing_site_rejected=1 macro_rept=3 lexical_set=1 numeric_renumber=1 counter_renumber=1 GNU_fill=exact NOBITS=exact'
