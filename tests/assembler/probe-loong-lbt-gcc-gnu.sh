#!/usr/bin/env bash
# Linux 6.6.143 arch/loongarch/Kconfig uses `as-instr,movscr2gr $a0,$scr0`
# to detect the LBT extension. Probe the *real* instruction encoding and its
# reciprocal transfer, not a special-cased Kconfig or shell success response.
set -Eeuo pipefail

: "${MINIAS:?provide MiniAS executable}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/lbt.s" <<'ASM'
.text
.globl loong_lbt_probe
loong_lbt_probe:
    movscr2gr $a0, $scr0
    movscr2gr $r31, $scr3
    movgr2scr $scr0, $a0
    movgr2scr $scr3, $r31
ASM

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64s \
  -o "$work/mini.o" "$work/lbt.s"

# GNU opcodes/loongarch-opc.c source: 0x00000c00 (r0:5,cr5:2),
# 0x00000800 (cr0:2,r5:5). This exact machine-code check still executes when
# a local developer lacks the cross GNU assembler; CI additionally compares
# the generated .text bytes against an independently assembled GNU object.
text_hex=$(readelf -x .text "$work/mini.o" |
  awk '/^[[:space:]]+0x[0-9a-fA-F]+/ {for (i=2; i<=NF; i++) {if ($i ~ /^[0-9a-fA-F]{8}$/) printf "%s", $i}}')
if [[ $text_hex != 040c00007f0c000080080000e30b0000 ]]; then
  echo "MINIAS_LOONG_LBT_GNU=FAIL expected_opcode_bytes actual=$text_hex" >&2
  exit 1
fi

if [[ ${MINIAS_LOONG_SKIP_GNU:-0} != 1 ]]; then
  : "${LOONGAS:?provide GNU LoongArch assembler}"
  : "${LOONGCOPY:?provide GNU LoongArch objcopy}"
  "$LOONGAS" -mabi=lp64s -o "$work/gnu.o" "$work/lbt.s"
  "$LOONGCOPY" -O binary -j .text "$work/mini.o" "$work/mini.bin"
  "$LOONGCOPY" -O binary -j .text "$work/gnu.o" "$work/gnu.bin"
  cmp "$work/mini.bin" "$work/gnu.bin"
fi

for invalid in \
  'movscr2gr $a0,$scr4' \
  'movscr2gr $a0,$r0' \
  'movscr2gr $scr0,$a0' \
  'movgr2scr $scr4,$a0' \
  'movgr2scr $r0,$a0' \
  'movgr2scr $scr0,$scr1' \
  'movscr2gr $a0,$scr-1' \
  'movscr2gr $a0,$scr32'; do
  printf '.text\n%s\n' "$invalid" >"$work/invalid.s"
  if "$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64s \
    -o "$work/invalid.o" "$work/invalid.s" >/dev/null 2>&1; then
    echo "MINIAS_LOONG_LBT_GNU=FAIL invalid_accepted=$invalid" >&2
    exit 1
  fi
done

if [[ ${MINIAS_LOONG_SKIP_GNU:-0} == 1 ]]; then
  echo 'MINIAS_LOONG_LBT_GNU=PASS kernel_probe=1 instructions=4 invalid_rejected=8 GNU_bytes=NOT_TESTED opcode_reference=binutils'
else
  echo 'MINIAS_LOONG_LBT_GNU=PASS kernel_probe=1 instructions=4 invalid_rejected=8 GNU_bytes=exact'
fi
