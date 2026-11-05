#!/usr/bin/env bash
# A 48-KiB source file is snapshotted at lexical parse, not reopened
# between multiple layout passes. Verify bytes against each real GNU AS.
set -Eeuo pipefail
: "${MINIAS:?}"
: "${ARCH:?}"
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT
case "$ARCH" in
x86_64) mini=(--target=x86_64); gas=(as --64); copy=(objcopy);;
riscv) mini=(-march=rv64i -mabi=lp64); gas=(riscv64-linux-gnu-as -march=rv64i -mabi=lp64); copy=(riscv64-linux-gnu-objcopy);;
aarch64) mini=(--target=aarch64); gas=(aarch64-linux-gnu-as); copy=(aarch64-linux-gnu-objcopy);;
loongarch64) mini=(--target=loongarch64); gas=(loongarch64-linux-gnu-as); copy=(loongarch64-linux-gnu-objcopy);;
*) exit 2;;
esac
python3 - "$w/payload.bin" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[1]).write_bytes(bytes(((i*31+17)&255) for i in range(49152)))
PY
printf '.data\n.incbin "%s/payload.bin"\n.byte 3,4,5\n.asciz "tail"\n' "$w" >"$w/incbin.s"
"$MINIAS" "${mini[@]}" -o "$w/mini.o" "$w/incbin.s"
"${gas[@]}" -o "$w/gnu.o" "$w/incbin.s"
"${copy[@]}" -j .data -O binary "$w/mini.o" "$w/mini.bin"
"${copy[@]}" -j .data -O binary "$w/gnu.o" "$w/gnu.bin"
cmp "$w/mini.bin" "$w/gnu.bin"
test "$(stat -c%s "$w/mini.bin")" -eq 49160
echo "MINIAS_INCBIN_IMMUTABLE=PASS arch=$ARCH GNU_49160_bytes_exact"
