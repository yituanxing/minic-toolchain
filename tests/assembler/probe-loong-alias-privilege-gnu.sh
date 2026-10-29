#!/usr/bin/env bash
# LoongArch ABI register aliases, one-source privileged descriptors, and
# Scalar-owned PCALA fixups.  GNU GAS owns cross-assembler byte/ELF semantics.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"

gnu_available=1
if ! command -v loongarch64-linux-gnu-as >/dev/null ||
   ! command -v loongarch64-linux-gnu-objcopy >/dev/null; then
  if [[ "${MINIAS_ALLOW_MISSING_GNU:-0}" == 1 ]]; then
    gnu_available=0
  else
    echo 'GNU LoongArch assembler and objcopy required for byte oracle' >&2
    exit 1
  fi
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'echo "MINIAS_LOONG_ALIAS_PRIVILEGE=FAIL command=$BASH_COMMAND" >&2' ERR

printf '%s\n' '.text' \
  'add.d $s9,$a0,$a1' \
  'movgr2fr.d $fa0,$a0' \
  'movfr2gr.d $a1,$ft15' \
  'fld.d $fs0,$sp,0' \
  'fst.s $fa7,$fp,-2048' >"$work/aliases.s"
printf '%s\n' '.text' \
  'add.d $r22,$r4,$r5' \
  'movgr2fr.d $f0,$r4' \
  'movfr2gr.d $r5,$f23' \
  'fld.d $f24,$r3,0' \
  'fst.s $f7,$r22,-2048' >"$work/numeric.s"
printf '%s\n' '.text' \
  'tlbclr' 'tlbflush' 'tlbsrch' 'tlbrd' 'tlbwr' 'tlbfill' 'ertn' \
  'nop' 'ret' \
  'pcalau12i $a0,%pc_hi20(external_symbol)' \
  'addi.d $a0,$a0,%pc_lo12(external_symbol)' >"$work/privilege-pcala.s"

for name in aliases numeric privilege-pcala; do
  "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/mini-$name.o" "$work/$name.s"
  readelf -h "$work/mini-$name.o" | grep -q 'Machine:.*LoongArch'
done

# Host objcopy often cannot read LoongArch.  Extract .text through readelf
# and compare its exact hex words without depending on cross tools.
readelf -x .text "$work/mini-aliases.o" | sed -n '/^  0x/p' >"$work/aliases.hex"
readelf -x .text "$work/mini-numeric.o" | sed -n '/^  0x/p' >"$work/numeric.hex"
cmp "$work/aliases.hex" "$work/numeric.hex"

readelf -Wr "$work/mini-privilege-pcala.o" >"$work/mini.reloc"
grep -q 'R_LARCH_PCALA_HI20' "$work/mini.reloc"
grep -q 'R_LARCH_PCALA_LO12' "$work/mini.reloc"

# The GNU source tree recognizes these bank aliases and rejects cross-bank
# and invalid relocation modifiers; reject locally even without GNU tools.
bad=(
  'movgr2fr.d $a0,$a1'
  'movfr2gr.d $a0,$a1'
  'fld.d $s0,$sp,0'
  'add.d $fa0,$a0,$a1'
  'movgr2fr.d $ft16,$a0'
  'movgr2fr.d $fs8,$a0'
  'add.d $s10,$a0,$a1'
  'nop $a0'
  'tlbclr $a0'
  'ret 1'
  'add.d $a0,$a0,%pc_lo12(external_symbol)'
  'pcalau12i $a0,%pc_lo12(external_symbol)'
  'addi.d $a0,$a0,%pc_hi20(external_symbol)'
)
for i in "${!bad[@]}"; do
  printf '.text\n%s\n' "${bad[$i]}" >"$work/bad$i.s"
  if "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
      -o "$work/bad$i.o" "$work/bad$i.s" 2>"$work/bad$i.err"; then
    echo "accepted bad LoongArch operand: ${bad[$i]}" >&2
    exit 1
  fi
  test ! -e "$work/bad$i.o"
done

if ((gnu_available)); then
  for name in aliases numeric privilege-pcala; do
    loongarch64-linux-gnu-as -mabi=lp64d -o "$work/gnu-$name.o" \
      "$work/$name.s"
    loongarch64-linux-gnu-objcopy -j .text -O binary \
      "$work/mini-$name.o" "$work/mini-$name.bin"
    loongarch64-linux-gnu-objcopy -j .text -O binary \
      "$work/gnu-$name.o" "$work/gnu-$name.bin"
    cmp "$work/mini-$name.bin" "$work/gnu-$name.bin"
  done
  python3 - "$work/mini-privilege-pcala.o" \
            "$work/gnu-privilege-pcala.o" <<'PY'
import re
import subprocess
import sys

def pcala_relocations(path):
    result=[]
    for line in subprocess.check_output(['readelf','-Wr',path],text=True).splitlines():
        columns=line.split()
        if (len(columns)>=5 and re.fullmatch('[0-9a-fA-F]{8,16}',columns[0])
                and columns[2] in {'R_LARCH_PCALA_HI20','R_LARCH_PCALA_LO12'}):
            result.append((columns[0],columns[2],tuple(columns[4:])))
    return result

mini,gnu=map(pcala_relocations,sys.argv[1:])
if mini!=gnu or len(mini)!=2:
    raise SystemExit(f'PCALA relocation mismatch: MiniAS={mini} GNU={gnu}')
PY
  for i in "${!bad[@]}"; do
    if loongarch64-linux-gnu-as -mabi=lp64d -o "$work/gnu-bad$i.o" \
        "$work/bad$i.s" >"$work/gnu-bad$i.out" 2>&1; then
      echo "GNU accepted purported negative: ${bad[$i]}" >&2
      exit 1
    fi
  done
  oracle=exact
else
  oracle='NOT_TESTED:explicit_local_no_cross_GNU'
fi
echo "MINIAS_LOONG_ALIAS_PRIVILEGE=PASS aliases=5 privileged=9 PCALA=2 negatives=${#bad[@]} GNU_bytes=$oracle GNU_relocations=$oracle no_partial_object=1"
