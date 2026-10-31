#!/usr/bin/env bash
# GNU .set is lexical: a previously emitted .long must NOT change when
# a later .set redefines the same absolute symbol.
set -Eeuo pipefail
: "${MINIAS:?}"
: "${ARCH:?}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
case "$ARCH" in
  x86_64) mini=(--target=x86_64); gas=(as --64); copy=(objcopy);;
  riscv) mini=(-march=rv64i -mabi=lp64); gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64); copy=(riscv64-linux-gnu-objcopy);;
  aarch64) mini=(--target=aarch64); gas=(aarch64-linux-gnu-as); copy=(aarch64-linux-gnu-objcopy);;
  loongarch64) mini=(--target=loongarch64); gas=(loongarch64-linux-gnu-as); copy=(loongarch64-linux-gnu-objcopy);;
  *) exit 2;;
esac
cat >"$work/lexical.s" <<'SRC'
.data
.set counter,5
.long counter*3
.set counter,7
.long counter*3
.set counter,9
.long (counter+1)<<2
.long counter - 2
SRC
"$MINIAS" "${mini[@]}" -o "$work/mini.o" "$work/lexical.s"
"${gas[@]}" -o "$work/gnu.o" "$work/lexical.s"
"${copy[@]}" -j .data -O binary "$work/mini.o" "$work/mini.bin"
"${copy[@]}" -j .data -O binary "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"
python3 - "$work/mini.bin" "$ARCH" <<'PY'
import struct,sys
b=open(sys.argv[1],'rb').read()
actual=struct.unpack('<4I',b)
expected=(15,21,40,7)
if actual!=expected:
    raise SystemExit(f'MCExpr lexical .set is not immutable: {actual} != {expected}')
print('MINIAS_MCEXPR_LEXICAL_SET=PASS arch='+sys.argv[2]+' values=4')
PY
