#!/usr/bin/env bash
set -Eeuo pipefail
minic="$1";test -x "$minic"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
for updates in 45 640;do
  python3 - "$updates" "$d/large-$updates.i" <<'PY'
import sys
n=int(sys.argv[1]);out=sys.argv[2];mask=(1<<64)-1;v=17
lines=['typedef unsigned long u64;','static u64 published;',
 '__attribute__((noinline)) static u64 heavy(u64 x) {']
for i in range(n):
    k=(i*83+17)%65521
    lines.append(' x = ((x << 3) ^ (x >> 2)) + %dUL;' % k)
    v=(((v << 3) ^ (v >> 2)) + k) & mask
lines.extend([' asm volatile("stlr %x1, %0" : "=Q"(published) : "rZ"(x) : "memory");',
 ' return published;','}',
 'int main(void) { return heavy(17UL) == %dUL ? 42 : 1; }' % v])
open(out,'w').write('\n'.join(lines)+'\n')
PY
  "$minic" --target=aarch64-linux -S "$d/large-$updates.i" -o "$d/large-$updates.s"
  grep -Fq 'stlr x9, [x10]' "$d/large-$updates.s"
  # Register allocation and Common Core spill-slot reuse can legitimately
  # eliminate the old x17 materialization. Keep the actual byte-offset
  # boundary test independent of that incidental register choice.
  python3 - "$d/large-$updates.s" "$updates" <<'PY'
import re
import sys

with open(sys.argv[1], encoding='utf-8') as stream:
    assembly = stream.read()
updates = int(sys.argv[2])
heavy = assembly.split('heavy:\n', 1)[1].split('.globl main', 1)[0]
match = re.search(r'\bmovz x(\d+), #(\d+)\s*\n\s*sub sp, sp, x\1\b', heavy)
if not match:
    raise SystemExit('missing real AArch64 heavy() stack allocation')
frame_bytes = int(match.group(2))
minimum = 512 if updates == 45 else 4095
if frame_bytes <= minimum:
    raise SystemExit(f'AArch64 frame {frame_bytes} did not cross {minimum}')
if updates == 640 and not re.search(r'\badd x(\d+), sp, x\1\b', heavy):
    raise SystemExit('missing large SP-relative address materialization')
print(f'AARCH64_LARGE_STRUCTURED_ASM_FRAME=PASS updates={updates} bytes={frame_bytes}')
PY
  if [[ "${MINIC_SOURCE_ONLY:-0}" != 1 ]];then
    aarch64-linux-gnu-gcc -static -O0 -x c "$d/large-$updates.i" -o "$d/gnu-$updates"
    aarch64-linux-gnu-gcc -static -x assembler "$d/large-$updates.s" -o "$d/mini-$updates"
    for compiler in gnu mini;do
      code=0;timeout 45s qemu-aarch64 "$d/$compiler-$updates" || code=$?
      if [[ "$code" != 42 ]];then
        echo "large-spill $updates $compiler exit=$code" >&2;exit 1
      fi
    done
  fi
  echo "AARCH64_LARGE_STRUCTURED_ASM_SPILL=PASS updates=$updates"
done
sed 's/stlr %x1, %0/str %x1, %0/' "$d/large-45.i" > "$d/unsafe.i"
if "$minic" --target=aarch64-linux -S "$d/unsafe.i" -o "$d/unsafe.s" >/dev/null 2>&1;then
  echo 'AArch64 target accepted wrong hardware ISA template' >&2;exit 1
fi
test ! -e "$d/unsafe.s"
if [[ "${MINIC_SOURCE_ONLY:-0}" == 1 ]]; then
  echo 'AARCH64_LARGE_STRUCTURED_ASM_SOURCE=PASS 512_boundary=1 4095_boundary=1 negatives=1 GNU_QEMU=SKIP'
else
  echo 'AARCH64_LARGE_STRUCTURED_ASM=PASS 512_boundary=1 4095_boundary=1 GNU_QEMU_42=2 negatives=1'
fi
