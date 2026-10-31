#!/usr/bin/env bash
# A GNU byte/relocation oracle for nested affine MCExpr lowering. The parser
# must bind one typed syntax tree, not split arithmetic at textual '+'/'-'.
set -Eeuo pipefail
: "${MINIAS:?}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/affine.s" <<'ASM'
.text
.globl affine_fn
.type affine_fn,@function
affine_fn:
    nop
    .space 7
.size affine_fn,.-affine_fn+2

.data
local_a:
    .space 16
local_b:
    .quad external_symbol+((9*3)-7)
    .quad (7+external_symbol)-3
    .quad ((external_symbol+12)-(local_a+4))
    .quad local_b-(local_a+4)
    .quad (local_b-local_a)/4
    .quad ((local_b-local_a)>>2)
    .long (local_a-.)+5
    .long .-(local_a+7)
ASM
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/affine.s"
as --64 -o "$work/gnu.o" "$work/affine.s"
objcopy -j .data -O binary "$work/mini.o" "$work/mini.bin"
objcopy -j .data -O binary "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"

# Compare normalized relocation semantics, not ELF symbol table ordinals.
python3 - "$work/mini.o" "$work/gnu.o" <<'PY'
import re
import subprocess
import sys

def relocations(path):
    records=[]
    for line in subprocess.check_output(['readelf','-rW',path],text=True).splitlines():
        match=re.search(r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(R_X86_64_\w+)\s+[0-9a-fA-F]+\s+(\S+)\s+([+-])\s+(\S+)',line)
        if match:
            records.append((int(match[1],16),match[2],match[3],match[4],match[5]))
    return records

mini,gnu=map(relocations,sys.argv[1:])
if mini != gnu or len(mini)!=3:
    raise SystemExit(f'affine reloc mismatch: {mini} != {gnu}')
PY

for obj in "$work/mini.o" "$work/gnu.o"; do
    readelf -sW "$obj" | grep -Eq '[[:space:]]10[[:space:]]+FUNC[[:space:]]+GLOBAL.*affine_fn$'
done

cat >"$work/bad.s" <<'ASM'
.data
    .quad (external_symbol+other_external)*2
ASM
if "$MINIAS" --target=x86_64 -o "$work/bad.o" "$work/bad.s" >/dev/null 2>&1; then
    echo 'MiniAS accepted non-affine two-symbol multiplication' >&2
    exit 1
fi
test ! -e "$work/bad.o"
echo 'MINIAS_MCEXPR_AFFINE_AST=PASS GNU_data=exact GNU_relocations=3 size=10 nonlinear=rejected'
