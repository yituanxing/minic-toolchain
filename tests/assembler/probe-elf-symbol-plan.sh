#!/usr/bin/env bash
# The ELF symbol plan must preserve COMDAT keys, prune disposable local labels,
# and keep external relocation identities in a genuine high-cardinality object.
# This deliberately combines N local labels, N fixups and a group signature:
# an O(N^2) per-symbol fixup scan is inappropriate for real Kbuild input.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

{
    printf '.section .rodata.symbol_plan,"aG",@progbits,plan_signature,comdat\n'
    printf '.globl plan_signature\n.type plan_signature,@object\nplan_signature:\n'
    for ((i=0;i<2048;++i)); do
        printf '.Llocal_%d:\n.long external_symbol + 4\n' "$i"
    done
    printf '.size plan_signature,.-plan_signature\n'
} >"$work/symbol-plan.s"

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/symbol-plan.s"
as --64 -o "$work/gnu.o" "$work/symbol-plan.s"
objcopy -j .rodata.symbol_plan -O binary "$work/mini.o" "$work/mini.data"
objcopy -j .rodata.symbol_plan -O binary "$work/gnu.o" "$work/gnu.data"
cmp "$work/mini.data" "$work/gnu.data"
readelf -rW "$work/mini.o" >"$work/mini.reloc"
readelf -rW "$work/gnu.o" >"$work/gnu.reloc"
readelf -Ws "$work/mini.o" >"$work/mini.syms"
readelf -Ws "$work/gnu.o" >"$work/gnu.syms"
readelf --section-groups "$work/mini.o" >"$work/mini.groups"
readelf --section-groups "$work/gnu.o" >"$work/gnu.groups"

python3 - "$work" <<'PY'
import pathlib, re, sys
root=pathlib.Path(sys.argv[1])
for kind in ('mini','gnu'):
    syms=(root / f'{kind}.syms').read_text()
    relocs=(root / f'{kind}.reloc').read_text()
    groups=(root / f'{kind}.groups').read_text()
    if '.Llocal_' in syms:
        raise SystemExit(f'{kind}: disposable GNU local labels leaked to ELF symtab')
    if not re.search(r'\bGLOBAL\s+DEFAULT\s+\d+\s+plan_signature\b',syms):
        raise SystemExit(f'{kind}: group signature missing from symtab')
    if not re.search(r'COMDAT group section.*\[plan_signature\]',groups):
        raise SystemExit(f'{kind}: SHT_GROUP signature missing')
    offsets=[int(m.group(1),16) for m in re.finditer(
        r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+R_X86_64_32\s+\S+\s+external_symbol\s+\+\s+4\s*$',
        relocs,re.M)]
    if offsets!=[i*4 for i in range(2048)]:
        raise SystemExit(f'{kind}: relocation offsets/count mismatch: {len(offsets)}')
mini=(root / 'mini.reloc').read_text()
gnu=(root / 'gnu.reloc').read_text()
if mini.count('R_X86_64_32')!=gnu.count('R_X86_64_32'):
    raise SystemExit('GNU relocation count diverged')
print('MINIAS_ELF_SYMBOL_PLAN=PASS temporary_pruning=2048 '
      'external_relocations=2048 COMDAT_signature=preserved GNU_data=exact')
PY

# The same plan decides whether a local relocation is folded into an
# STT_SECTION reference and whether its .L SymbolID remains live. Exercise
# folded and un-folded references in one large object: a second independent
# folding decision during relocation writing could break the ordinal map.
{
    printf '.section .rodata.fold_plan,"aG",@progbits,fold_signature,comdat\n'
    printf '.globl fold_signature\n.type fold_signature,@object\nfold_signature:\n'
    for ((i=0;i<2048;++i)); do
        printf '.Lfold_%d:\n.long .Lfold_%d\n.long external_symbol + 4\n' "$i" "$i"
    done
    printf '.size fold_signature,.-fold_signature\n'
} >"$work/fold-plan.s"

"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/fold-plan.s"
as --64 -o "$work/gnu.o" "$work/fold-plan.s"
for who in mini gnu; do
    objcopy -j .rodata.fold_plan -O binary "$work/$who.o" "$work/$who.data"
    readelf -rW "$work/$who.o" >"$work/$who.reloc"
    readelf -Ws "$work/$who.o" >"$work/$who.syms"
    readelf --section-groups "$work/$who.o" >"$work/$who.groups"
done
cmp "$work/mini.data" "$work/gnu.data"

python3 - "$work" <<'PY'
from pathlib import Path
import re
import sys

root=Path(sys.argv[1])
for who in ('mini','gnu'):
    syms=(root / f'{who}.syms').read_text()
    relocs=(root / f'{who}.reloc').read_text()
    groups=(root / f'{who}.groups').read_text()
    if '.Lfold_' in syms:
        raise SystemExit(f'{who}: section-folded private labels leaked to .symtab')
    if not re.search(r'COMDAT group section.*\[fold_signature\]',groups):
        raise SystemExit(f'{who}: folded-relocation COMDAT signature missing')
    section=[]
    external=[]
    for line in relocs.splitlines():
        row=re.match(r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+'
                     r'R_X86_64_32\s+[0-9a-fA-F]+\s+(\S+)',line)
        if row is None:
            continue
        offset=int(row.group(1),16)
        if row.group(2)=='.rodata.fold_plan':
            section.append(offset)
        elif row.group(2)=='external_symbol':
            external.append(offset)
        else:
            raise SystemExit(f'{who}: unexpected relocation symbol: {line}')
    if section!=[8*i for i in range(2048)] or \
       external!=[8*i+4 for i in range(2048)]:
        raise SystemExit(f'{who}: mixed relocation map mismatch: '
                         f'folded={len(section)} external={len(external)}')
print('MINIAS_ELF_REFERENCE_PLAN=PASS section_folded=2048 '
      'external_relocations=2048 temporary_pruning=2048 '
      'COMDAT_signature=preserved GNU_data=exact')
PY

# A grouping-only object has no fixups, yet its undefined default-local
# signature is intentionally created by Common and retained in SHT_GROUP.
# This exercises the empty relocation plan and non-emitted local labels.
cat >"$work/group-only.s" <<'ASM'
.section .rodata.group_only,"aG",@progbits,group_only_signature,comdat
.long 0x12345678
ASM
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/group-only.s"
as --64 -o "$work/gnu.o" "$work/group-only.s"
for who in mini gnu; do
    readelf -rW "$work/$who.o" >"$work/$who.reloc"
    readelf -Ws "$work/$who.o" >"$work/$who.syms"
    readelf --section-groups "$work/$who.o" >"$work/$who.groups"
done
python3 - "$work" <<'PY'
from pathlib import Path
import re
import sys

root=Path(sys.argv[1])
for who in ('mini','gnu'):
    reloc=(root / f'{who}.reloc').read_text()
    syms=(root / f'{who}.syms').read_text()
    groups=(root / f'{who}.groups').read_text()
    if 'There are no relocations in this file' not in reloc:
        raise SystemExit(f'{who}: group-only object unexpectedly has relocations')
    if not re.search(r'LOCAL\s+DEFAULT\s+\d+\s+group_only_signature\b',syms):
        raise SystemExit(f'{who}: undefined local COMDAT signature missing')
    if not re.search(r'COMDAT group section.*\[group_only_signature\]',groups):
        raise SystemExit(f'{who}: grouping-only SHT_GROUP invalid')
print('MINIAS_ELF_REFERENCE_EMPTY=PASS fixups=0 '
      'undefined_local_COMDAT_signature=preserved GNU=exact')
PY

# AArch64's Target ABI forbids folding an STT_FUNC relocation to a SECTION
# symbol (a linker veneer needs the function identity), but still folds an
# ordinary local data reference. This specifically tests the negative path
# of the same per-Fixup decision. The cross-GNU byte oracle is separate.
cat >"$work/a64-fold-policy.s" <<'ASM'
.text
.type .Lkept_function,@function
.Lkept_function:
    ret
.data
.Lfolded_data:
    .quad 0x55
    .quad .Lkept_function
    .quad .Lfolded_data
ASM
"$MINIAS" --target=aarch64 -o "$work/a64-fold-policy.o" \
    "$work/a64-fold-policy.s"
readelf -rW "$work/a64-fold-policy.o" >"$work/a64-fold-policy.reloc"
readelf -Ws "$work/a64-fold-policy.o" >"$work/a64-fold-policy.syms"
python3 - "$work" <<'PY'
from pathlib import Path
import re
import sys

root=Path(sys.argv[1])
relocs=(root / 'a64-fold-policy.reloc').read_text()
syms=(root / 'a64-fold-policy.syms').read_text()
references=[]
for line in relocs.splitlines():
    row=re.match(r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+'
                 r'R_AARCH64_ABS64\s+[0-9a-fA-F]+\s+(\S+)',line)
    if row:
        references.append((int(row.group(1),16),row.group(2)))
if references != [(8,'.Lkept_function'),(16,'.data')]:
    raise SystemExit(f'AArch64 local function/data folding diverged: {references}')
if not re.search(r'FUNC\s+LOCAL\s+DEFAULT\s+\d+\s+\.Lkept_function\b',syms):
    raise SystemExit('AArch64 symbol-sensitive relocation lost function identity')
if '.Lfolded_data' in syms:
    raise SystemExit('AArch64 folded data symbol unnecessarily retained')
print('MINIAS_ELF_REFERENCE_VETO=PASS arch=aarch64 '
      'STT_FUNC=preserved ordinary_data=section_folded')
PY
