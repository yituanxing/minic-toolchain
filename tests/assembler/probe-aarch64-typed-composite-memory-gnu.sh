#!/usr/bin/env bash
# GNU AArch64 oracles for the unified typed Memory descriptor and composite
# deferred ALU/branch operands. Two mutable .set sites must retain their
# lexical meaning; delayed section deltas resolve after final layout.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
gnu_available=1
if ! command -v aarch64-linux-gnu-as >/dev/null ||
   ! command -v aarch64-linux-gnu-objcopy >/dev/null; then
  if [[ "${MINIAS_ALLOW_MISSING_GNU:-0}" == 1 ]]; then
    gnu_available=0
  else
    echo 'GNU AArch64 assembler and objcopy are mandatory for CI' >&2
    exit 1
  fi
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "MINIAS_A64_TYPED_COMPOSITE=FAIL command=$BASH_COMMAND" >&2' ERR

cat >"$work/composite.s" <<'ASM'
.text
.globl entry
entry:
.set n,16
  ldr x0,[sp,#n]
.set n,24
  ldr x1,[sp,#n]
  stp x2,x3,[sp,#-n]!
  ldp x2,x3,[sp],#n
start:
  .space 8
end:
  ldr x0,[x1,#end-start]
  str w2,[x3,#start-end]
  add x4,x4,#end-start
  cmp x4,#end-start
  b target+(end-start)
target:
  ret
ASM

cat >"$work/memory.s" <<'ASM'
.text
  ldr x5,[x6,w7,sxtw #3]
  prfm pldl1keep,[x1,x2,lsl #3]
  stp q20,q21,[sp,#-32]!
  ld1 {v0.16b,v1.16b},[x1],#32
  .arch armv8.5-a+lse
  ldadd x0,x1,[x2]
  .arch armv8.5-a+memtag
  stzg x0,[sp,#16]
ASM

for case_name in composite memory; do
  "$MINIAS" --target=aarch64 -o "$work/$case_name.mini.o" \
    "$work/$case_name.s"
  readelf -h "$work/$case_name.mini.o" | grep -q 'Machine:.*AArch64'
  if ((gnu_available)); then
    aarch64-linux-gnu-as -o "$work/$case_name.gnu.o" "$work/$case_name.s"
    for tool in mini gnu; do
      aarch64-linux-gnu-objcopy -j .text -O binary \
        "$work/$case_name.$tool.o" "$work/$case_name.$tool.bin"
    done
    cmp "$work/$case_name.mini.bin" "$work/$case_name.gnu.bin"
  fi
done

for invalid in \
  'ldr x0,[sp,#-257]' \
  'ld1 {v0.16b,v1.16b},[x1],#31' \
  'prfm pldl1keep,[x1,x2,lsl #9]'; do
  printf '.text\n%s\n' "$invalid" >"$work/bad.s"
  if "$MINIAS" --target=aarch64 -o "$work/bad.o" "$work/bad.s" \
      2>"$work/bad.err"; then
    echo "accepted invalid A64 typed Memory: $invalid" >&2; exit 1
  fi
  test ! -e "$work/bad.o"
done

# Reject the previous source-text reparsing paths after the typed migration.
python3 - <<'PY'
from pathlib import Path
import re
src=Path('assembler/src/aarch64_target.c').read_text()
src=re.sub(r'/\*[\s\S]*?\*/|//[^\n]*',' ',src)
for legacy in ('a64_fold_absolute_memory_address(',
               'a64_replace_argument_range(',
               'a64_capture_deferred_difference('):
    if legacy in src:
        raise SystemExit('A64 retained legacy text-based Memory path: '+legacy)
PY

if ((gnu_available)); then oracle=exact; else oracle='NOT_TESTED:explicit_local_no_cross_GNU'; fi
echo "MINIAS_A64_TYPED_COMPOSITE=PASS GNU_bytes=$oracle lexical_sets=2 memory_forms=10 composite=5 negatives=3 dead_text_parsers=0"
