#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
command -v as >/dev/null
command -v readelf >/dev/null

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# .debug_frame CIE identity must include *all* pre-first-PC CFI directives.
# f/g share a CIE; h has a different initial CFA; s is `simple` and has no
# implicit CIE CFA/return-address state. FDE-only operations must not affect
# initial CIE identity, even when the source's functions interleave.
cat >"$work/debug.s" <<'ASM'
.cfi_sections .debug_frame
.text
.globl debug_cfi_f
debug_cfi_f:
.cfi_startproc
.cfi_def_cfa 7,24
nop
.cfi_def_cfa_offset 32
nop
.cfi_endproc
.globl debug_cfi_g
debug_cfi_g:
.cfi_startproc
.cfi_def_cfa 7,24
nop
.cfi_def_cfa_offset 48
nop
.cfi_endproc
.globl debug_cfi_h
debug_cfi_h:
.cfi_startproc
.cfi_def_cfa 7,16
nop
.cfi_endproc
.globl debug_cfi_s
debug_cfi_s:
.cfi_startproc simple
nop
.cfi_endproc
ASM

# A .cfi_restore after the first instruction reverts to the CIE's initial
# register state, NOT to the latest state introduced by an earlier FDE op.
# GNU GAS places the pre-first-PC rbp save in the shared CIE; omitting it from
# the CIE makes the unwind state after `restore` observably wrong at runtime.
cat >"$work/eh.s" <<'ASM'
.text
.globl eh_cfi_f
eh_cfi_f:
.cfi_startproc
.cfi_offset 6,-16
nop
.cfi_restore 6
nop
.cfi_endproc
.globl eh_cfi_g
eh_cfi_g:
.cfi_startproc
.cfi_offset 6,-16
nop
.cfi_def_cfa_offset 24
nop
.cfi_restore 6
nop
.cfi_endproc
ASM

for kind in debug eh; do
  as --64 -o "$work/$kind.gnu.o" "$work/$kind.s"
  "$MINIAS" --target=x86_64 -o "$work/$kind.mini.o" "$work/$kind.s"
  readelf --debug-dump=frames "$work/$kind.gnu.o" >"$work/$kind.gnu.frames"
  readelf --debug-dump=frames "$work/$kind.mini.o" >"$work/$kind.mini.frames"
done

python3 - "$work" <<'PY'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])

def parse(kind, producer):
    content = (root / f'{kind}.{producer}.frames').read_text()
    cies, fdes = [], []
    current = None
    for line in content.splitlines():
        cie = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+CIE\b', line)
        fde = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+FDE\s+cie=([0-9a-f]{8})\b', line)
        if cie:
            current = {'addr': cie.group(1), 'version': None, 'events': []}
            cies.append(current)
        elif fde:
            current = {'addr': fde.group(1), 'cie': fde.group(2), 'events': []}
            fdes.append(current)
        elif current is not None:
            version = re.search(r'^\s*Version:\s*(\d+)', line)
            if version:
                current['version'] = int(version.group(1))
            if 'DW_CFA_' in line:
                event = line.split('DW_CFA_', 1)[1].strip()
                if event != 'nop':
                    current['events'].append(event)
    if not all(fde['cie'] in {cie['addr'] for cie in cies} for fde in fdes):
        raise SystemExit(f'{kind} {producer}: dangling CIE reference: {content}')
    return cies, fdes

def structural_view(cies, fdes):
    cie_map = {cie['addr']: i for i, cie in enumerate(cies)}
    return ({'cie_count': len(cies), 'fde_count': len(fdes),
             'cie_versions': [cie['version'] for cie in cies],
             'cie_events': [cie['events'] for cie in cies],
             'fde_cies': [cie_map[fde['cie']] for fde in fdes],
             'fde_events': [fde['events'] for fde in fdes]})

for kind, expected_cies, expected_fdes in [('debug', 3, 4), ('eh', 1, 2)]:
    gnu = structural_view(*parse(kind, 'gnu'))
    mini = structural_view(*parse(kind, 'mini'))
    if gnu['cie_count'] != expected_cies or gnu['fde_count'] != expected_fdes:
        raise SystemExit(f'GNU expected {expected_cies}/{expected_fdes} {kind} CIE/FDE, got {gnu}')
    if gnu != mini:
        raise SystemExit(f'{kind} initial-state GNU mismatch:\nGNU={gnu}\nMiniAS={mini}')

    if kind == 'debug':
        assert gnu['fde_cies'] == [0, 0, 1, 2], gnu
        assert gnu['cie_versions'] == [1, 1, 1], gnu
        assert any('def_cfa: r7' in x and 'ofs 24' in x for x in gnu['cie_events'][0]), gnu
        assert not gnu['cie_events'][2], gnu
    else:
        assert gnu['fde_cies'] == [0, 0], gnu
        assert any('offset: r6' in x and 'cfa-16' in x for x in gnu['cie_events'][0]), gnu
        assert all(any('restore: r6' in x for x in fde) for fde in gnu['fde_events']), gnu

    print(f'MINIAS_CFI_INITIAL_STATE=PASS kind={kind} CIE={expected_cies} FDE={expected_fdes} GNU_semantic_program=exact')
PY
