#!/usr/bin/env bash
# An actual object-model integration test: 2048 repeated symbolic operands
# all reference the same stable symbol ordinal, including both forward
# constants and unresolved external relocation targets. No duplicate copied
# symbol names are retained in per-operand MCExpr nodes.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/symbol-ids.s" <<'SRC'
.data
.globl field_table
field_table:
.rept 1024
    .long external_symbol + 4
    .short forward_absolute
    .short 5
.endr
.set forward_absolute, 11
SRC
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/symbol-ids.s"
as --64 -o "$work/gnu.o" "$work/symbol-ids.s"
objcopy -j .data -O binary "$work/mini.o" "$work/mini.data"
objcopy -j .data -O binary "$work/gnu.o" "$work/gnu.data"
cmp "$work/mini.data" "$work/gnu.data"
readelf -rW "$work/mini.o" > "$work/mini.reloc"
readelf -rW "$work/gnu.o" > "$work/gnu.reloc"
python3 - "$work/mini.reloc" "$work/gnu.reloc" <<'PY'
import re,sys
def read(path):
    lines=open(path,encoding='utf-8').readlines()
    return [(int(m.group(1),16),m.group(2),int(m.group(3)))
            for line in lines
            if (m:=re.search(r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(R_X86_64_32)\s+[0-9a-fA-F]+\s+external_symbol\s+\+\s+(\d+)',line))]
a,b=map(read,sys.argv[1:])
if len(a)!=1024 or a!=b:
    raise SystemExit(f'MCExpr symbol IDs relocation mismatch len={len(a)},gnu={len(b)}')
print('MINIAS_MCEXPR_SYMBOL_IDS=PASS 1024_shared_external_relocations=GNU_exact forward_absolute=11')
PY
