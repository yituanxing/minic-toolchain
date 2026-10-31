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
