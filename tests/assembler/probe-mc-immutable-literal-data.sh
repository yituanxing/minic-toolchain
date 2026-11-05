#!/usr/bin/env bash
# GNU oracle for immutable Common encoded data fragments, including
# signed/unsigned LEB width transitions and zero/escaped string literals.
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
cat >"$w/data.s" <<'SRC'
.data
.uleb128 0
.uleb128 127
.uleb128 128
.uleb128 16384
.sleb128 -1
.sleb128 -64
.sleb128 63
.sleb128 64
.ascii "alpha\000beta"
.asciz "gamma", "delta"
.string "epsilon"
.set mutable,17
.uleb128 mutable
.set mutable,33
.uleb128 mutable
SRC
"$MINIAS" "${mini[@]}" -o "$w/mini.o" "$w/data.s"
"${gas[@]}" -o "$w/gnu.o" "$w/data.s"
"${copy[@]}" -j .data -O binary "$w/mini.o" "$w/mini.bin"
"${copy[@]}" -j .data -O binary "$w/gnu.o" "$w/gnu.bin"
if ! cmp "$w/mini.bin" "$w/gnu.bin"; then
  echo "Common lexical DATA bytes mismatch GNU: $ARCH" >&2
  od -An -tx1 "$w/mini.bin" >&2
  od -An -tx1 "$w/gnu.bin" >&2
  exit 1
fi
echo "MINIAS_IMMUTABLE_LITERAL=PASS arch=$ARCH encoded_once=1 GNU_exact=1"
