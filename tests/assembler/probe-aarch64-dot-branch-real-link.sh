#!/usr/bin/env bash
# AArch64 Linux busy-wait and nVHE code frequently branch to the current PC.
# GNU GAS encodes these as immediate offsets, never relocations against `.`.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

printf '%s\n' '.text' '.global dot_branch_entry' 'dot_branch_entry:' \
  'b .' 'b .+4' 'b .-4' 'bl .' 'b.eq .' 'b.ne .+8' \
  'cbz w0,.' 'cbnz x1,.+8' 'tbz w2,#3,.' 'tbnz x3,#40,.-4' \
  'ret' >"$work/dot.s"
"$minias" --target=aarch64 -o "$work/dot-mini.o" "$work/dot.s"
readelf -x .text "$work/dot-mini.o" >"$work/dot.hex"
readelf -Wr "$work/dot-mini.o" >"$work/dot.relocs"
readelf -Ws "$work/dot-mini.o" >"$work/dot.symbols"

# All 10 self/dot-relative branches encode directly, without ELF fixups.
if grep -q 'R_AARCH64_' "$work/dot.relocs"; then
  echo 'AARCH64_DOT_BRANCH=FAIL dot produced ELF relocation' >&2
  cat "$work/dot.relocs" >&2
  exit 1
fi
if grep -Eq 'UND[[:space:]]+\.' "$work/dot.symbols"; then
  echo 'AARCH64_DOT_BRANCH=FAIL bogus undefined dot symbol' >&2
  cat "$work/dot.symbols" >&2
  exit 1
fi
for encoded in \
  '00000014 01000014 ffffff17 00000094' \
  '00000054 41000054 00000034 410000b5' \
  '02001836 e3ff47b7 c0035fd6'; do
  if ! grep -qi "$encoded" "$work/dot.hex"; then
    echo "AARCH64_DOT_BRANCH=FAIL missing=$encoded" >&2
    cat "$work/dot.hex" >&2
    exit 1
  fi
done

# External branches must retain the typed relocation, proving the self-PC
# expression case has not disabled normal GNU BFD link resolution.
printf '%s\n' '.text' 'b.eq external_branch_destination' >"$work/ext.s"
"$minias" --target=aarch64 -o "$work/ext.o" "$work/ext.s"
readelf -Wr "$work/ext.o" >"$work/ext.relocs"
grep -q 'R_AARCH64_CONDBR19' "$work/ext.relocs"
grep -q 'external_branch_destination' "$work/ext.relocs"

# Reject an unaligned location counter target, even though it names no ELF
# symbol. All branch forms share the same 4-byte alignment requirement.
printf '%s\n' '.text' 'b.ne .+1' >"$work/invalid.s"
if "$minias" --target=aarch64 -o "$work/invalid.o" "$work/invalid.s" \
  >"$work/invalid.log" 2>&1; then
  echo 'AARCH64_DOT_BRANCH=FAIL accepted misaligned dot target' >&2
  exit 1
fi

if command -v aarch64-linux-gnu-as >/dev/null 2>&1 &&
   command -v aarch64-linux-gnu-objcopy >/dev/null 2>&1; then
  aarch64-linux-gnu-as -o "$work/dot-gnu.o" "$work/dot.s"
  aarch64-linux-gnu-objcopy -j .text -O binary \
    "$work/dot-gnu.o" "$work/dot-gnu.bin"
  aarch64-linux-gnu-objcopy -j .text -O binary \
    "$work/dot-mini.o" "$work/dot-mini.bin"
  cmp "$work/dot-gnu.bin" "$work/dot-mini.bin"
fi

echo 'MINIAS_AARCH64_DOT_BRANCH=PASS GNU_PC_imm=10 no_dot_reloc=1 external_reloc=1 bad_alignment=reject'
