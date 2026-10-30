#!/usr/bin/env bash
# GNU GAS oracle for `.loc view -0` and ELF symbol sizes measured from a
# different same-section entry label. These affect genuine Linux i386 VDSO
# objects, but both contracts belong to Common DWARF/MCLayout, not x86.
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in as ld readelf python3; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/size.s" <<'ASM'
.text
.globl entry
.type entry,@function
.p2align 2
entry:
.Lentry:
  jmp .Lend
  .fill 300,1,0x90
.Lend:
  ret
.size entry,.-.Lentry

.globl inner
.type inner,@function
inner:
  nop
.Linner:
  nop
  ret
.size inner,.-.Linner

.subsection 2
.globl cross
.type cross,@function
cross:
.Lcross:
  nop
.subsection 3
  nop
  ret
.size cross,.-.Lcross
ASM

for width in 32 64; do
  if [[ $width == 32 ]]; then
    target=(-march=i386 -mabi=i386)
    emulation=elf_i386
  else
    target=()
    emulation=elf_x86_64
  fi
  as --"$width" -o "$work/gnu-$width.o" "$work/size.s"
  "$MINIAS" --target=x86_64 "${target[@]}" \
    -o "$work/mini-$width.o" "$work/size.s"
  ld -m "$emulation" -r -o "$work/gnu-$width-linked.o" "$work/gnu-$width.o"
  ld -m "$emulation" -r -o "$work/mini-$width-linked.o" "$work/mini-$width.o"
  readelf -sW "$work/gnu-$width-linked.o" >"$work/gnu-$width.syms"
  readelf -sW "$work/mini-$width-linked.o" >"$work/mini-$width.syms"
  python3 - "$width" "$work/gnu-$width.syms" "$work/mini-$width.syms" <<'PY'
import pathlib
import sys

width, gnu_path, mini_path = sys.argv[1:]
def read(path):
    symbols = {}
    for line in pathlib.Path(path).read_text().splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[3] == 'FUNC' and parts[-1] in ('entry','inner','cross'):
            symbols[parts[-1]] = int(parts[2])
    return symbols
gnu, mini = read(gnu_path), read(mini_path)
assert set(gnu) == {'entry','inner','cross'}, (width,gnu)
assert gnu == mini, (width,gnu,mini)
assert gnu['entry'] >= 300 and gnu['inner'] == 2 and gnu['cross'] == 3, (width,gnu)
print(f'MINIAS_DWARF_SIZE_ALIAS=PASS ELF{width} GNU_ld_r=1 sizes={gnu}')
PY
done

cat >"$work/view.s" <<'ASM'
.file 1 "a.c"
.text
.loc 1 1 0 view -0
.loc 1 2 0 view -0
nop
.loc 1 3 0 view -0
nop
ASM
as --32 -o "$work/gnu-view.o" "$work/view.s"
"$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
  -o "$work/mini-view.o" "$work/view.s"
readelf --debug-dump=rawline "$work/gnu-view.o" >"$work/gnu-view.txt"
readelf --debug-dump=rawline "$work/mini-view.o" >"$work/mini-view.txt"
python3 - "$work/gnu-view.txt" "$work/mini-view.txt" <<'PY'
import pathlib
import re
import sys

gnu,mini=[pathlib.Path(s).read_text() for s in sys.argv[1:]]
def count(s): return len(re.findall(r'Extended opcode 2: set Address to',s))
assert count(gnu) == 2, count(gnu)
assert count(mini) == count(gnu), (count(gnu),count(mini))
assert 'Line by 1 to 2' in gnu and 'Line by 1 to 3' in gnu
assert 'Advance Line by 1 to 2' in mini and 'Advance Line by 1 to 3' in mini
print('MINIAS_DWARF_VIEW_RESET=PASS reset_rows=2 semantic_GNU_line_state=equal')
PY

for view in '0' '5'; do
  cat >"$work/bad.s" <<ASM
.file 1 "a.c"
.text
.loc 1 1 0
.loc 1 2 0 view $view
nop
ASM
  # view 0 fails because same PC is already on the next view; view 5
  # also mismatches GNU's expected view number for this repeated PC.
  if as --32 -o "$work/gnu-bad.o" "$work/bad.s" >/dev/null 2>&1; then
    echo "MINIAS_DWARF_VIEW_FAIL GNU_accepted=$view" >&2
    exit 1
  fi
  if "$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
    -o "$work/bad.o" "$work/bad.s" >/dev/null 2>&1; then
    echo "MINIAS_DWARF_VIEW_FAIL accepted=$view" >&2
    exit 1
  fi
done
echo 'MINIAS_DWARF_VIEW_REJECT=PASS mismatch=2'
