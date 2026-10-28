#!/usr/bin/env bash
# 160 independent conditional branches to a late subsection. This forces
# multiple realloc() steps for the long-branch owner array and triggers the
# legacy O(N^2) membership scan. Verify exact GNU bytes and persistent
# symbol anchors; no test-specific assembler behavior is allowed.
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
python3 - "$work/trace" <<'PY'
import pathlib,re,sys
s=pathlib.Path(sys.argv[1]).read_text()
if 'minias-fragment-relayout:inplace-passes=' not in s:
    raise SystemExit('Expected in-place branch relaxation: '+s)
m=re.search(r'minias-fragment-graph:anchors_built=1 relayouts=(\d+) branches=(\d+) reparse=0',s)
if not m or int(m.group(2))<100:
    raise SystemExit('Expected cached graph and 100+ branch expansions: '+s)
print('MINIAS_MCLAYOUT_STRESS=PASS branches='+m.group(2)+' relayouts='+m.group(1))
PY
