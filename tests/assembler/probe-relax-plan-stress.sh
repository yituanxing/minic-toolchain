#!/usr/bin/env bash
# Stress 160 x86-64 far branches. Stable symbols and force-expanded branches
# exercise Common MCLayout's persistent anchor graph and O(1) membership.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "RELAX_STRESS_FAIL command=$BASH_COMMAND" >&2; test ! -f "$work/trace" || cat "$work/trace" >&2' ERR

{
    echo '.text'
    echo '.globl branch_entry'
    echo 'branch_entry:'
    for ((i=0;i<160;i++)); do
        echo 'je branch_exit'
        echo '.space 16,0x90'
    done
    echo '.size branch_entry,.-branch_entry'
    echo '.subsection 1'
    echo 'branch_exit:'
    echo 'nop'
} >"$work/stress.s"

MINIAS_TRACE_RELAYOUT=1 "$MINIAS" --target=x86_64 \
    -o "$work/mini.o" "$work/stress.s" 2>"$work/trace"
as --64 -o "$work/gnu.o" "$work/stress.s"
objcopy -j .text -O binary "$work/mini.o" "$work/mini.bin"
objcopy -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"

readelf -Ws "$work/mini.o" >"$work/mini.sym"
readelf -Ws "$work/gnu.o" >"$work/gnu.sym"
python3 - "$work/mini.sym" "$work/gnu.sym" "$work/trace" <<'PY'
from pathlib import Path
import re,sys

def symbol(path):
    for line in Path(path).read_text().splitlines():
        columns=line.split()
        if len(columns)>=8 and columns[-1]=='branch_entry' and columns[1].endswith(':') is False:
            return int(columns[1],16),int(columns[2])
    raise SystemExit('No exported symbol branch_entry in '+path)
actual=symbol(sys.argv[1])
gnu=symbol(sys.argv[2])
if actual!=gnu:
    raise SystemExit(f'MCLayout symbol value/size mismatch MiniAS={actual} GNU={gnu}')
trace=Path(sys.argv[3]).read_text()
if 'minias-fragment-relayout:inplace-passes=' not in trace:
    raise SystemExit('Missing in-place relaxation trace '+trace)
match=re.search(r'minias-fragment-graph:anchors_built=1 relayouts=(\d+) branches=(\d+) reparse=0',trace)
if not match or int(match.group(2))<100:
    raise SystemExit('Expected 100+ expanded conditional branches: '+trace)
print('MINIAS_MCLAYOUT_STRESS=PASS branches='+match.group(2)+' relayouts='+match.group(1))
print('MINIAS_MCLAYOUT_SYMBOL_SIZE=PASS GNU_exact')
PY
