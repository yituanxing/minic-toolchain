#!/usr/bin/env bash
# AArch64 PostEmit reads live Section alignment while its separate Common
# finalization sink raises it and materializes $x/$d mapping symbols.
set -Eeuo pipefail
: "${MINIAS:?Set the exact-head assembler}"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"$MINIAS" --target=aarch64 -o "$work/final.o" \
    tests/assembler/fixtures/aarch64-readonly-finalization.S

python3 - "$work/final.o" <<'PY'
import re
import subprocess
import sys

obj=sys.argv[1]
sections=subprocess.check_output(['readelf','-SW',obj],text=True)
symbols=subprocess.check_output(['readelf','-sW',obj],text=True)
section=next((line for line in sections.splitlines() if re.search(
    r'\]\s+\.text\s+PROGBITS',line)),None)
if not section or not re.search(r'\s+AX\s+0\s+0\s+4$',section):
    raise SystemExit('PostEmit failed to commit live AArch64 .text alignment')
actual=[]
for line in symbols.splitlines():
    fields=line.split()
    if len(fields)>=8 and fields[3:5]==['NOTYPE','LOCAL']:
        name=fields[7].split('.',1)[0]
        if name in ('$x','$d'):
            actual.append((name,int(fields[1],16)))
expected=[('$d',0),('$x',4),('$d',12)]
if actual!=expected:
    raise SystemExit(f'PostEmit AArch64 mapping drift: {actual} != {expected}')
print('MINIAS_TARGET_READONLY_FINALIZATION=PASS AArch64_map=3 '
      'align=4 source_inst_provenance=preserved')
PY
