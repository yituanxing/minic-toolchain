#!/usr/bin/env bash
# Loong Target token lifetimes may no longer depend on a fixed 192-byte
# stack operand buffer. GNU GAS bytes/PCALA relocations are the oracle.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
gnu_available=1
if ! command -v loongarch64-linux-gnu-as >/dev/null ||
   ! command -v loongarch64-linux-gnu-objcopy >/dev/null; then
  if [[ "${MINIAS_ALLOW_MISSING_GNU:-0}" == 1 ]]; then
    gnu_available=0
  else
    echo 'GNU LoongArch assembler and objcopy are required for CI' >&2
    exit 1
  fi
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "MINIAS_LOONG_DYNAMIC_TOKENS=FAIL command=$BASH_COMMAND" >&2' ERR

python3 - "$work/dynamic.s" <<'PY'
from pathlib import Path
import sys
lines=['.text','entry:']
for target in (191,192,512):
    count=(target-1)//2
    body='('+'+'.join(['1']*count)+')'
    body=body[:-1]+' '*(target-len(body))+')'
    assert len(body)==target
    lines.extend((f'  addi.d $a0,$a1,{body}',
                  f'  li.d $a2,{body}'))
lines.extend(('  pcalau12i $a0,%pc_hi20(external_symbol)',
              '  addi.d $a0,$a0,%pc_lo12(external_symbol)',
              '  bgt $a0,$a1,nearby',
              '  ble $a0,$a1,nearby',
              '  bgtu $a0,$a1,nearby',
              '  bleu $a0,$a1,nearby',
              'nearby:',
              '  addi.d $a0,$a0,1'))
Path(sys.argv[1]).write_text('\n'.join(lines)+'\n',encoding='utf-8')
PY

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/mini.o" "$work/dynamic.s"
readelf -h "$work/mini.o" | grep -q 'Machine:.*LoongArch'
readelf -Wr "$work/mini.o" >"$work/mini.reloc"
grep -q 'R_LARCH_PCALA_HI20' "$work/mini.reloc"
grep -q 'R_LARCH_PCALA_LO12' "$work/mini.reloc"

if ((gnu_available)); then
  loongarch64-linux-gnu-as -mabi=lp64d -o "$work/gnu.o" "$work/dynamic.s"
  for tool in mini gnu; do
    loongarch64-linux-gnu-objcopy -j .text -O binary \
      "$work/$tool.o" "$work/$tool.bin"
  done
  if ! cmp "$work/mini.bin" "$work/gnu.bin"; then
    echo 'LoongArch dynamic source GNU text-byte oracle diverged' >&2
    for tool in mini gnu; do
      echo "--- $tool .text bytes ---" >&2
      od -An -tx1 -w16 "$work/$tool.bin" >&2
      echo "--- $tool relocations ---" >&2
      readelf -Wr "$work/$tool.o" >&2
    done
    exit 1
  fi
  python3 - "$work/mini.o" "$work/gnu.o" <<'PY'
import re
import subprocess
import sys
def relocs(path):
    out=[]
    for line in subprocess.check_output(['readelf','-Wr',path],text=True).splitlines():
        columns=line.split()
        if len(columns)>=5 and re.fullmatch('[0-9a-fA-F]{8,16}',columns[0]) and columns[2].startswith('R_LARCH_'):
            out.append((columns[0],columns[2],tuple(columns[4:])))
    return out
mini,gnu=relocs(sys.argv[1]),relocs(sys.argv[2])
if mini!=gnu:
    raise SystemExit(f'Loong typed relocation ownership differs: {mini} != {gnu}')
PY
fi

bad=('addi.d $a0,$a1,(1+2' 'addi.d $a0,$a1,"not_an_immediate"'
     'addi.d $a0,$a1,1,$a2,$a3')
for i in "${!bad[@]}"; do
  printf '.text\n%s\n' "${bad[$i]}" >"$work/bad$i.s"
  if "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
      -o "$work/bad$i.o" \
      "$work/bad$i.s" 2>"$work/bad$i.err"; then
    echo "accepted malformed Loong operand ${bad[$i]}" >&2; exit 1
  fi
  test ! -e "$work/bad$i.o"
done
python3 - "$work/too_long_symbol.s" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[1]).write_text('.text\naddi.d $a0,$a1,'+'s'*256+'\n')
PY
if "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/too_long_symbol.o" \
    "$work/too_long_symbol.s" 2>"$work/too_long_symbol.err"; then
  echo 'accepted overlength Loong symbol' >&2; exit 1
fi
test ! -e "$work/too_long_symbol.o"

if ((gnu_available)); then oracle=exact; else oracle='NOT_TESTED:explicit_local_no_cross_GNU'; fi
echo "MINIAS_LOONG_DYNAMIC_TOKENS=PASS operands=191,192,512 PCALA=2 GNU_bytes_relocs=$oracle negatives=4 no_partial_object=1"
