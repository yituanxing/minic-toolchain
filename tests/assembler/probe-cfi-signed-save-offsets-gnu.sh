#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
command -v as >/dev/null
command -v readelf >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for mode in debug_frame eh_frame; do
  cat >"$work/$mode.s" <<ASM
.cfi_sections .$mode
.text
.globl cfi_signed_f
cfi_signed_f:
.cfi_startproc
.cfi_offset 6,8
.cfi_val_offset 7,16
.cfi_def_cfa 7,24
.cfi_rel_offset 11,40
nop
.cfi_offset 300,24
.cfi_val_offset 301,8
.cfi_rel_offset 12,0
nop
.cfi_offset 8,-16
.cfi_val_offset 9,-24
nop
.cfi_endproc
.globl cfi_signed_g
cfi_signed_g:
.cfi_startproc
.cfi_offset 6,8
.cfi_val_offset 7,16
.cfi_def_cfa 7,24
.cfi_rel_offset 11,40
nop
.cfi_offset 8,-8
.cfi_rel_offset 13,32
nop
.cfi_endproc
ASM
  as --64 -o "$work/$mode.gnu.o" "$work/$mode.s"
  "$MINIAS" --target=x86_64 -o "$work/$mode.mini.o" "$work/$mode.s"
  readelf --debug-dump=frames "$work/$mode.gnu.o" >"$work/$mode.gnu.frames"
  readelf --debug-dump=frames "$work/$mode.mini.o" >"$work/$mode.mini.frames"
done

python3 - "$work" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])

def parse(mode, producer):
    text = (root / f'{mode}.{producer}.frames').read_text()
    cie, fde = [], []
    active = None
    for line in text.splitlines():
        c = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+CIE\b', line)
        f = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+FDE\s+cie=([0-9a-f]{8})\b', line)
        if c:
            active = {'offset': c.group(1), 'events': []}
            cie.append(active)
        elif f:
            active = {'cie': f.group(2), 'events': []}
            fde.append(active)
        elif active is not None and 'DW_CFA_' in line:
            event = line.split('DW_CFA_', 1)[1].strip()
            if event != 'nop':
                active['events'].append(event)
    offsets = {x['offset']: i for i, x in enumerate(cie)}
    return {'cie_events': [x['events'] for x in cie],
            'fde_cies': [offsets[x['cie']] for x in fde],
            'fde_events': [x['events'] for x in fde]}

for mode in ['debug_frame', 'eh_frame']:
    gnu = parse(mode, 'gnu')
    mini = parse(mode, 'mini')
    if gnu != mini:
        raise SystemExit(f'{mode} GNU signed-CFI mismatch:\nGNU={gnu}\nMiniAS={mini}')
    assert len(gnu['cie_events']) == 1 and len(gnu['fde_events']) == 2, gnu
    assert gnu['fde_cies'] == [0, 0], gnu
    initial = '\n'.join(gnu['cie_events'][0])
    fde = '\n'.join(gnu['fde_events'][0])
    assert 'offset_extended_sf: r6' in initial and 'cfa+8' in initial, gnu
    assert 'val_offset_sf: r7' in initial and 'cfa+16' in initial, gnu
    assert 'offset_extended_sf: r11' in initial and 'cfa+16' in initial, gnu
    assert 'offset_extended_sf:' in fde and 'r300 at cfa+24' in fde, gnu
    assert 'val_offset_sf:' in fde and 'r301 is cfa+8' in fde, gnu
    assert 'offset: r12' in fde and 'cfa-24' in fde, gnu
    assert 'offset: r8' in fde and 'cfa-16' in fde, gnu
    assert 'val_offset: r9' in fde and 'cfa-24' in fde, gnu
    print(f'MINIAS_CFI_SIGNED_SAVE=PASS section=.{mode} CIE=1 FDE=2 GNU_semantic_program=exact')
PY

for directive in '.cfi_offset 6,-10' '.cfi_val_offset 7,10' '.cfi_rel_offset 6,7'; do
  cat >"$work/invalid.s" <<ASM
.cfi_sections .debug_frame
.text
f:
.cfi_startproc
$directive
nop
.cfi_endproc
ASM
  if as --64 -o "$work/invalid.gnu.o" "$work/invalid.s" >"$work/invalid.gnu.log" 2>&1; then
    echo "GNU unexpectedly accepted nonaligned $directive" >&2
    exit 1
  fi
  if "$MINIAS" --target=x86_64 -o "$work/invalid.mini.o" "$work/invalid.s" >"$work/invalid.mini.log" 2>&1; then
    echo "MiniAS unexpectedly accepted nonaligned $directive" >&2
    exit 1
  fi
done
echo 'MINIAS_CFI_SIGNED_SAVE_NEGATIVE=PASS GNU_reject=3 MiniAS_reject=3'
