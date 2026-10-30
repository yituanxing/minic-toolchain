#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in as ld readelf python3; do command -v "$tool" >/dev/null; done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Every FDE in .debug_frame needs a relocation for its CIE pointer, even
# when the CIE begins at offset zero in the original ET_REL.  GNU ld -r may
# concatenate multiple such sections, shifting later objects' CIE offsets.
# Reading individual objects alone cannot catch the missing relocation.
cat >"$work/source.s" <<'ASM'
.cfi_sections .debug_frame
.text
one:
.cfi_startproc
nop
.cfi_endproc
two:
.cfi_startproc
nop
.cfi_endproc
ASM

for bits in 32 64; do
  as "--$bits" -o "$work/gnu.o" "$work/source.s"
  if [[ "$bits" == 32 ]]; then
    "$MINIAS" --target=x86_64 -march=i386 -mabi=i386 \
      -o "$work/mini.o" "$work/source.s"
    emulation=elf_i386
  else
    "$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/source.s"
    emulation=elf_x86_64
  fi

  # GNU reference and mixed GNU+MiniAS must give the same CIE/FDE graph
  # after the linker adjusts all .debug_frame section-base relocations.
  ld -r -m "$emulation" -o "$work/gg.o" "$work/gnu.o" "$work/gnu.o"
  ld -r -m "$emulation" -o "$work/gm.o" "$work/gnu.o" "$work/mini.o"
  readelf --debug-dump=frames "$work/gg.o" >"$work/gg.frames"
  readelf --debug-dump=frames "$work/gm.o" >"$work/gm.frames"

  python3 - "$bits" "$work/gg.frames" "$work/gm.frames" <<'PY'
import pathlib
import re
import sys

bits, reference, actual = sys.argv[1:]

def parse(path):
    cies, fdes, current = [], [], None
    for line in pathlib.Path(path).read_text().splitlines():
        cie = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+CIE\b', line)
        fde = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]+\s+[0-9a-f]+\s+FDE\s+cie=([0-9a-f]{8})\b', line)
        if cie:
            current = dict(addr=cie.group(1), rules=[])
            cies.append(current)
        elif fde:
            current = dict(cie=fde.group(2), rules=[])
            fdes.append(current)
        elif current is not None and 'DW_CFA_' in line:
            rule = line.split('DW_CFA_', 1)[1].strip()
            if rule != 'nop' and not rule.startswith('advance_loc'):
                current['rules'].append(rule)
    addresses = {cie['addr']: i for i, cie in enumerate(cies)}
    assert len(cies) == 2 and len(fdes) == 4, (path, cies, fdes)
    assert all(fde['cie'] in addresses for fde in fdes), (path, cies, fdes)
    return {'cie_rules': [cie['rules'] for cie in cies],
            'fde_cies': [addresses[fde['cie']] for fde in fdes],
            'fde_rules': [fde['rules'] for fde in fdes]}

gnu, mini = parse(reference), parse(actual)
assert gnu['fde_cies'] == [0, 0, 1, 1], (bits, gnu)
if mini != gnu:
    raise SystemExit(f'ELF{bits} ld -r CIE-link mismatch:\nGNU={gnu}\nMini={mini}')
print(f'MINIAS_CFI_LINK=PASS ELF{bits} GNU_ld_r CIE=2 FDE=4 linked_CIE_refs=exact')
PY
done

# A GNU RISC-V cross linker is not required for this structural contract:
# the common writer must emit an R_RISCV_32 CIE-pointer relocation in each
# RISC-V ELF64 ET_REL too, independent of x86/i386 target specifics.
"$MINIAS" --target=riscv -march=rv64i -mabi=lp64 \
    -o "$work/riscv.o" "$work/source.s"
readelf -Wr "$work/riscv.o" >"$work/riscv.reloc"
python3 - "$work/riscv.reloc" <<'PY'
import pathlib
import re
import sys

content = pathlib.Path(sys.argv[1]).read_text()
section = re.search(r"Relocation section '\.rela.debug_frame'.*?(?=Relocation section|\Z)",
                    content, re.S)
assert section, content
# Each FDE contributes a CIE section-relative relocation, plus its PC.
types = re.findall(r'\b(R_RISCV_\w+)\b', section.group())
assert types.count('R_RISCV_32') == 2, types
print('MINIAS_CFI_LINK=PASS RV64 .debug_frame CIE_relocations=R_RISCV_32x2')
PY
