#!/usr/bin/env bash
# Linux Kbuild x86 locks are often separate AT&T `lock;` prefixes, whereas
# real-mode code freely mixes byte-sized operations and segment instructions.
# Compare real GNU GAS bytes and ELF32/ELF64 relocs before enabling a form.
set -Eeuo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
minias=${MINIAS:-"$root/build/release/bin/minic-as"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/native.s" <<'ASM'
.text
.globl prefix_native
prefix_native:
    lock; addl $1, counter(%rip)
    lock; subl $1, counter(%rip)
    ret
.data
.globl counter
counter: .long 0
.section .note.GNU-stack,"",@progbits
ASM

cat >"$work/legacy.s" <<'ASM'
.text
.code16
.globl prefix_legacy
prefix_legacy:
    andb $0x3f, %al
    cmpb $0x42, %al
    mov %cr0, %eax
    .code32
    lock; addl $1, 8(%esp)
    lsl %ax, %eax
    ret
.section .note.GNU-stack,"",@progbits
ASM

for profile in native legacy; do
  if [[ $profile == legacy ]]; then
    flags=(--target=x86_64 -march=i386 -mabi=i386)
    as --32 -o "$work/gnu-$profile.o" "$work/$profile.s"
  else
    flags=(--target=x86_64)
    as --64 -o "$work/gnu-$profile.o" "$work/$profile.s"
  fi
  "$minias" "${flags[@]}" -o "$work/mini-$profile.o" "$work/$profile.s"
  for engine in gnu mini; do
    objcopy --only-section=.text -O binary "$work/$engine-$profile.o" \
      "$work/$engine-$profile.text"
    readelf -rW "$work/$engine-$profile.o" | \
      sed -n '/Relocation section/,/^$/p' >"$work/$engine-$profile.reloc"
  done
  cmp "$work/gnu-$profile.text" "$work/mini-$profile.text"
  python3 - "$work/gnu-$profile.reloc" "$work/mini-$profile.reloc" <<'PY'
from pathlib import Path
import sys
def relocs(p):
    rows=[]
    for line in Path(p).read_text().splitlines():
        fields=line.split()
        if len(fields)>=5 and fields[2].startswith('R_'):
            rows.append((int(fields[0],16),fields[2],fields[-1]))
    return rows
a,b=map(relocs,sys.argv[1:])
if a!=b:
    raise SystemExit(f'GNU relocation mismatch: GNU={a} MiniAS={b}')
PY
  echo "MINIAS_X86_LINUX_LOCK_PREFIX=PASS profile=$profile GNU_bytes=exact GNU_relocations=exact"
done
