#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the exact-head MiniAS binary}"
command -v as >/dev/null
command -v readelf >/dev/null
command -v objcopy >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# One source deliberately crosses Symbol/Fragment/Section/Object boundaries:
# forward section-relative aliases, a variable-size layout dependency, a
# parenthesized .org, same-name sections in separate COMDAT groups, and a
# GNU unnumbered file symbol. These were separate false-success paths in the
# original Common code; keep them in one end-to-end GNU object oracle.
cat >"$work/cross.s" <<'SRC'
.file "batch-a.c"
.set LIMIT,16
.text
base:
.set alias_forward,target
.equ equ_forward,target
.space payload_end-payload_begin,0
.org (base+LIMIT)
target: .byte 0x90
.section .rodata,"a",@progbits
payload_begin: .byte 1,2,3,4,5,6,7,8
payload_end:
.section .text.same,"axG",@progbits,group_first,comdat
.byte 0x90
.section .text.same,"axG",@progbits,group_second,comdat
.byte 0xc3
SRC

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/cross.s"
as --64 -o "$work/gnu.o" "$work/cross.s"
for producer in mini gnu; do
  readelf -SW "$work/$producer.o" >"$work/$producer.sections"
  readelf -sW "$work/$producer.o" >"$work/$producer.symbols"
  readelf -gW "$work/$producer.o" >"$work/$producer.groups"
  objcopy -O binary --only-section=.text "$work/$producer.o" \
    "$work/$producer.text"
done
cmp "$work/mini.text" "$work/gnu.text"

python3 - "$work" <<'PY'
import pathlib
import re
import sys

root=pathlib.Path(sys.argv[1])
def inspect(producer):
    sec=(root/f'{producer}.sections').read_text()
    sym=(root/f'{producer}.symbols').read_text()
    groups=(root/f'{producer}.groups').read_text()
    text=(root/f'{producer}.text').read_bytes()
    if len(text)!=17 or text[-1]!=0x90:
        raise SystemExit(f'{producer}: incorrect variable .space/.org: {text.hex()}')
    if len(re.findall(r'\s\.text\.same\s',sec))!=2:
        raise SystemExit(f'{producer}: lost duplicate-name COMDAT sections\n{sec}')
    if len(re.findall(r'COMDAT group section',groups))!=2:
        raise SystemExit(f'{producer}: lost group signatures\n{groups}')
    for name in ('target','alias_forward','equ_forward'):
        line=next((s for s in sym.splitlines() if re.search(r'\s'+name+r'$',s)),None)
        if line is None or not re.search(r'\s+0000000000000010\s+\d+\s+',line):
            raise SystemExit(f'{producer}: bad final alias {name}: {line}\n{sym}')
        if re.search(r'\sABS\s',line):
            raise SystemExit(f'{producer}: alias {name} unexpectedly ABS: {line}')
    if not re.search(r'FILE\s+LOCAL\s+DEFAULT\s+ABS\s+batch-a\.c',sym):
        raise SystemExit(f'{producer}: no GNU STT_FILE metadata\n{sym}')
    return text
assert inspect('mini')==inspect('gnu')
print('MINIAS_MC_CROSS_LAYER=PASS section=2_groups aliases=3 file_symbol=1 org=17')
PY

cat >"$work/bad.s" <<'SRC'
.section .mini_test,"az",@progbits
.byte 1
SRC
if "$MINIAS" --target=x86_64 -o "$work/mini-bad.o" "$work/bad.s" \
    >"$work/mini.out" 2>&1; then
  echo 'MiniAS silently accepted unknown section flag' >&2
  exit 1
fi
if as --64 -o "$work/gnu-bad.o" "$work/bad.s" \
    >"$work/gnu.out" 2>&1; then
  echo 'GNU AS did not reject unknown section flag' >&2
  exit 1
fi
echo 'MINIAS_MC_CROSS_LAYER_NEGATIVE=PASS unknown_section_flag=reject'

# x86 executable alignment is a target-owned code policy, not just a byte
# count. Repeated 0x90 bytes are semantically harmless but do not match GNU's
# canonical multi-byte NOP sequence for an otherwise identical input.
for leading in 1 5 9 13; do
  cat >"$work/align.s" <<SRC
.text
.rept $leading
nop
.endr
.p2align 4
ret
SRC
  "$MINIAS" --target=x86_64 -o "$work/mini-align.o" "$work/align.s"
  as --64 -o "$work/gnu-align.o" "$work/align.s"
  objcopy -O binary --only-section=.text "$work/mini-align.o" \
    "$work/mini-align.text"
  objcopy -O binary --only-section=.text "$work/gnu-align.o" \
    "$work/gnu-align.text"
  cmp "$work/mini-align.text" "$work/gnu-align.text"
done
echo 'MINIAS_X86_GNU_ALIGNMENT=PASS samples=4 canonical_nop_bytes=equal'
