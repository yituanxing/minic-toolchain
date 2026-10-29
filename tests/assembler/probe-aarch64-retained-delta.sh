#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/late.s" <<'SRC'
.text
.globl layout_delta
layout_delta:
pre:
.space span_end-span_begin,0
post:
add x0,x0,#post-pre
sub x1,x1,#post-pre
ldr x2,[x3,#post-pre]
str x2,[x3,#post-pre]
ldr s0,[x4,#post-pre]
str s0,[x4,#post-pre]
ldr d0,[x4,#post-pre]
stp x5,x6,[x7,#post-pre]
ldp x5,x6,[x7,#post-pre]
b destination+(post-pre)
b.eq destination+(post-pre)
destination:
nop
.extern external_destination
b external_destination+8
.section .rodata,"a",@progbits
span_begin: .byte 1,2,3,4,5,6,7,8
span_end:
SRC

"$MINIAS" --target=aarch64 -o "$work/mini.o" "$work/late.s"
if ! command -v aarch64-linux-gnu-as >/dev/null ||
   ! command -v aarch64-linux-gnu-objcopy >/dev/null; then
  echo 'MINIAS_A64_RETAINED_DELTA=SKIP(GNU-toolchain-unavailable) MiniAS=ASSEMBLED'
  exit 0
fi
aarch64-linux-gnu-as -o "$work/gnu.o" "$work/late.s"
for producer in mini gnu; do
  aarch64-linux-gnu-objcopy --dump-section .text="$work/$producer.text" \
    "$work/$producer.o"
  readelf -rW "$work/$producer.o" >"$work/$producer.reloc"
  grep -Eq 'R_AARCH64_JUMP26.*external_destination.*\+ 8' \
    "$work/$producer.reloc"
done
if ! cmp "$work/mini.text" "$work/gnu.text"; then
  echo 'AArch64 retained-delta GNU text-byte oracle diverged' >&2
  for producer in mini gnu; do
    echo "--- $producer .text bytes ---" >&2
    od -An -tx1 -w16 "$work/$producer.text" >&2
    echo "--- $producer relocations ---" >&2
    readelf -Wr "$work/$producer.o" >&2
  done
  exit 1
fi
echo 'MINIAS_A64_RETAINED_DELTA=PASS GNU_bytes=equal ADD_SUB_LDR_STR_FP_PAIR_BRANCH=1 external_JUMP26=1'

for invalid in \
  'add x0,x0,#4097' \
  'ldr x0,[x1,#32768]' \
  'stp x0,x1,[x2,#513]'; do
  printf '.text\n%s\n' "$invalid" >"$work/invalid.s"
  if "$MINIAS" --target=aarch64 -o "$work/invalid-mini.o" \
      "$work/invalid.s" >"$work/mini.stdout" 2>&1; then
    echo "MiniAS accepted A64 out-of-range immediate: $invalid" >&2
    exit 1
  fi
  if aarch64-linux-gnu-as -o "$work/invalid-gnu.o" \
      "$work/invalid.s" >"$work/gnu.stdout" 2>&1; then
    echo "GNU GAS accepted presumed-negative A64 immediate: $invalid" >&2
    exit 1
  fi
done
echo 'MINIAS_A64_RETAINED_DELTA_NEGATIVE=PASS bounds=3'
