#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in as readelf objcopy cmp python3; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# i386 ELF32 ABI is not AMD64 code under a smaller ELF header.  The CFI
# register map, data slots, saved return address and frame alignment are all
# target-profile properties.  Exercise both frame types against GNU as --32.
# Include a pre-first-PC initial rule, post-PC FDE rules, a simple frame and
# an explicit return-column override (also used by Linux signal trampolines).
for mode in .eh_frame .debug_frame; do
  cat >"$work/cfi.s" <<ASM
.cfi_sections $mode
.text
.globl frame_a
frame_a:
.cfi_startproc
.cfi_def_cfa_offset 8
nop
.cfi_offset 5,-8
nop
.cfi_restore 5
.cfi_endproc
.globl frame_b
frame_b:
.cfi_startproc
.cfi_return_column 0
nop
.cfi_endproc
.globl frame_c
frame_c:
.cfi_startproc simple
.cfi_def_cfa 4,4
nop
.cfi_endproc
ASM
  as --32 -o "$work/gnu.o" "$work/cfi.s"
  "$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
      -o "$work/mini.o" "$work/cfi.s"

  readelf -h "$work/mini.o" | grep -q 'Class:.*ELF32'
  readelf -h "$work/mini.o" | grep -q 'Machine:.*Intel 80386'
  readelf -SW "$work/gnu.o" >"$work/gnu.sections"
  readelf -SW "$work/mini.o" >"$work/mini.sections"
  readelf --debug-dump=frames "$work/gnu.o" >"$work/gnu.frames"
  readelf --debug-dump=frames "$work/mini.o" >"$work/mini.frames"
  readelf -Wr "$work/gnu.o" >"$work/gnu.relocations"
  readelf -Wr "$work/mini.o" >"$work/mini.relocations"
  objcopy --dump-section "$mode=$work/gnu.frame.bytes" "$work/gnu.o"
  objcopy --dump-section "$mode=$work/mini.frame.bytes" "$work/mini.o"

  python3 - "$mode" "$work" <<'PY'
import pathlib
import re
import sys

mode, root = sys.argv[1], pathlib.Path(sys.argv[2])

def parse(who):
    text = (root / f'{who}.frames').read_text()
    cies, fdes, current = [], [], None
    for line in text.splitlines():
        cie = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+CIE\b', line)
        fde = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+FDE\s+cie=([0-9a-f]{8})\b', line)
        if cie:
            current = dict(addr=cie.group(1), code=None, data=None,
                           return_column=None, rules=[])
            cies.append(current)
        elif fde:
            current = dict(cie=fde.group(2), rules=[])
            fdes.append(current)
        elif current is not None:
            for field, name in [('Code alignment factor', 'code'),
                                ('Data alignment factor', 'data'),
                                ('Return address column', 'return_column')]:
                m = re.search(field + r':\s*(-?\d+)', line)
                if m: current[name] = int(m.group(1))
            if 'DW_CFA_' in line:
                event = line.split('DW_CFA_', 1)[1].strip()
                if event != 'nop' and not event.startswith('advance_loc'):
                    current['rules'].append(event)
    indices = {cie['addr']: i for i, cie in enumerate(cies)}
    return dict(cies=[{k: v for k, v in cie.items() if k != 'addr'}
                      for cie in cies],
                fdes=[dict(cie=indices[fde['cie']], rules=fde['rules'])
                      for fde in fdes])

gnu, mini = parse('gnu'), parse('mini')
assert len(gnu['fdes']) == 3, (mode, gnu)
assert all(cie['data'] == -4 and cie['code'] == 1 for cie in gnu['cies']), gnu
assert [cie['return_column'] for cie in gnu['cies']] == [8, 0, 8], gnu
if gnu != mini:
    raise SystemExit(f'{mode} i386 frame mismatch:\nGNU={gnu}\nMiniAS={mini}')

def alignment(who):
    sections = (root / f'{who}.sections').read_text().splitlines()
    for line in sections:
        if re.search(r'\]\s+' + re.escape(mode) + r'\s+', line):
            return int(line.split()[-1])
    raise SystemExit(f'{who} missing section {mode}')

assert alignment('gnu') == alignment('mini') == 4, (mode, alignment('gnu'), alignment('mini'))

def cfi_relocs(who):
    text = (root / f'{who}.relocations').read_text()
    match = re.search(r"Relocation section '\.rel" + re.escape(mode) +
                      r"'.*?(?=Relocation section|\Z)", text, re.S)
    assert match is not None, (who, mode, text)
    return re.findall(r'^\s*([0-9a-f]{8})\s+[0-9a-f]{8}\s+'
                      r'(R_386_\w+)\s+', match.group(), re.M)

gnu_relocs, mini_relocs = cfi_relocs('gnu'), cfi_relocs('mini')
expected = 'R_386_PC32' if mode == '.eh_frame' else 'R_386_32'
expected_count = 3 if mode == '.eh_frame' else 6
assert len(gnu_relocs) == len(mini_relocs) == expected_count, (mode, gnu_relocs, mini_relocs)
assert gnu_relocs == mini_relocs, (mode, gnu_relocs, mini_relocs)
assert all(kind == expected for _, kind in gnu_relocs), (mode, gnu_relocs)
print(f'MINIAS_CFI_I386=PASS section={mode} CIE={len(gnu["cies"])} '
      f'FDE={len(gnu["fdes"])} GNU_unwind_rules=exact align=4 relocs=exact')
PY

  # The CIE/FDE CFI byte stream should also agree where GNU's physical
  # placement and relocation addends are identical (no symbol-rewrite
  # heuristics).  A mismatch remains a failure: the oracle is the emitted
  # real 32-bit object, not merely the presence of CFI directives.
  if ! cmp -s "$work/gnu.frame.bytes" "$work/mini.frame.bytes"; then
    echo "$mode i386 raw frame bytes differ from GNU GAS" >&2
    od -Ax -tx1 "$work/gnu.frame.bytes" >&2
    od -Ax -tx1 "$work/mini.frame.bytes" >&2
    exit 1
  fi
  echo "MINIAS_CFI_I386_RAW=PASS section=$mode GNU_frame_bytes=equal"
done
