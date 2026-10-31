#!/usr/bin/env bash
# Exercise the three relocation ownership domains through one ELF object:
# instruction/data fragment, named .reloc directive and synthesized DWARF.
set -Eeuo pipefail
: "${MINIAS:?MINIAS executable required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/owners.s" <<'SRC'
.text
.globl owner_probe
.type owner_probe, @function
owner_probe:
    nop
    call external_call
    .balign 16, 0
    .long external_data
    .reloc 0, R_X86_64_32, explicit_symbol
.size owner_probe, .-owner_probe
.data
    .quad external_table
SRC
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/owners.s"
as --64 -o "$work/gnu.o" "$work/owners.s"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.text"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.text"
cmp "$work/mini.text" "$work/gnu.text"
objcopy -j .data -O binary "$work/mini.o" "$work/mini.data"
objcopy -j .data -O binary "$work/gnu.o" "$work/gnu.data"
cmp "$work/mini.data" "$work/gnu.data"
readelf -rW "$work/mini.o" > "$work/mini.reloc"
readelf -rW "$work/gnu.o" > "$work/gnu.reloc"
python3 - "$work/mini.reloc" "$work/gnu.reloc" <<'PY'
import re
import sys
def parse(path):
    section = ''
    result = []
    for line in open(path, encoding='utf-8'):
        match = re.search(r"Relocation section '([^']+)'", line)
        if match:
            section = match.group(1)
        match = re.match(r'\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(R_X86_64_[A-Z0-9_]+)\s+[0-9a-fA-F]+\s+(external_call|external_data|explicit_symbol|external_table)\b', line)
        if match:
            result.append((section, int(match.group(1), 16), match.group(2), match.group(3)))
    return sorted(result)
mini, gnu = map(parse, sys.argv[1:])
if mini != gnu or len(mini) != 4:
    raise SystemExit(f"fragment fixup mismatch\nMiniAS: {mini}\nGNU: {gnu}")
print("MINIAS_FRAGMENT_FIXUP_OWNERSHIP=PASS instruction=1 data=2 explicit=1")
PY
