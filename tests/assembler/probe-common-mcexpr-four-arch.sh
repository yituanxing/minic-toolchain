#!/usr/bin/env bash
# GNU byte/reloc differential of identical MCExpr data directives on all
# four machine backends. ISA code emission is deliberately not involved.
set -Eeuo pipefail
: "${MINIAS:?}"
: "${ARCH:?}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
case "$ARCH" in
x86_64) mini=(--target=x86_64); gas=(as --64); copy=(objcopy);;
riscv) mini=(-march=rv64i -mabi=lp64); gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64); copy=(riscv64-linux-gnu-objcopy);;
aarch64) mini=(--target=aarch64); gas=(aarch64-linux-gnu-as); copy=(aarch64-linux-gnu-objcopy);;
loongarch64) mini=(--target=loongarch64); gas=(loongarch64-linux-gnu-as); copy=(loongarch64-linux-gnu-objcopy);;
*) exit 2;;
esac
cat >"$tmp/common.s" <<'SRC'
.data
.globl mc_expr_table
mc_expr_table:
local_base:
.long (5*3)+(8>>1)
.long external_data+12
.short forward_absolute
.short 5
.long .-local_base
.long .-local_base+7
.long .-(local_base+1)
.set forward_absolute, 31
SRC
"$MINIAS" "${mini[@]}" -o "$tmp/mini.o" "$tmp/common.s"
"${gas[@]}" -o "$tmp/gnu.o" "$tmp/common.s"
"${copy[@]}" -j .data -O binary "$tmp/mini.o" "$tmp/mini.bin"
"${copy[@]}" -j .data -O binary "$tmp/gnu.o" "$tmp/gnu.bin"
if ! cmp "$tmp/mini.bin" "$tmp/gnu.bin"; then
    echo "$ARCH byte mismatch" >&2
    od -An -tx1 "$tmp/mini.bin" >&2
    od -An -tx1 "$tmp/gnu.bin" >&2
    exit 1
fi
readelf -rW "$tmp/mini.o" > "$tmp/mini.reloc"
readelf -rW "$tmp/gnu.o" > "$tmp/gnu.reloc"
python3 - "$ARCH" "$tmp/mini.reloc" "$tmp/gnu.reloc" <<'PY'
import re,sys
arch,a,b=sys.argv[1:]
def find(path):
    lines=open(path,encoding='utf-8').readlines()
    return [(int(m.group(1),16),m.group(2),int(m.group(3),16))
            for line in lines
            if (m:=re.search(
              r'^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(R_[A-Z0-9_]+)'
              r'\s+[0-9a-fA-F]+\s+external_data\s+\+\s+([0-9a-fA-F]+)\s*]
x,y=map(find,(a,b))
if len(x)!=1 or len(y)!=1 or x!=y:
    raise SystemExit(f'{arch} MCExpr relocation mismatch: {x} vs {y}\nMiniAS: '+open(a).read()+'\nGNU: '+open(b).read())
print(f'MINIAS_MCEXPR_FOUR_ARCH=PASS arch={arch} GNU_bytes=exact GNU_reloc=exact')
PY
,line))]
x,y=map(find,(a,b))
if len(x)!=1 or len(y)!=1 or x!=y:
    raise SystemExit(f'{arch} MCExpr relocation mismatch: {x} vs {y}')
print(f'MINIAS_MCEXPR_FOUR_ARCH=PASS arch={arch} GNU_bytes=exact GNU_reloc=exact')
PY
