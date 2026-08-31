#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
AR=${RISCV_AR:-riscv64-linux-gnu-ar}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}
OBJCOPY=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}

work="$BUILD_DIR/tests/linker/a1"
rm -rf "$work"
mkdir -p "$work/lib"

cat >"$work/root.s" <<'EOF'
.text
.globl root
.type root, @function
root:
  call needed
  ret
.size root, .-root
EOF

cat >"$work/lib/needed.s" <<'EOF'
.text
.globl needed
.type needed, @function
needed:
  li a0, 9
  ret
.size needed, .-needed
EOF

cat >"$work/lib/unused.s" <<'EOF'
.text
.globl unused_member
.type unused_member, @function
unused_member:
  li a0, 77
  ret
.size unused_member, .-unused_member
EOF

"$AS" -march=rv64gc -mabi=lp64d -o "$work/root.o" "$work/root.s"
"$AS" -march=rv64gc -mabi=lp64d -o "$work/lib/needed.o" "$work/lib/needed.s"
"$AS" -march=rv64gc -mabi=lp64d -o "$work/lib/unused.o" "$work/lib/unused.s"

(
  cd "$work"
  "$AR" rcSTPD whole.a lib/needed.o lib/unused.o
  "$AR" rcsTPD group.a lib/needed.o lib/unused.o
)

"$LD" -melf64lriscv -r -o "$work/reference-whole.o" \
  --whole-archive "$work/whole.a" --no-whole-archive
"$MINILD" -melf64lriscv -r -o "$work/product-whole.o" \
  --whole-archive "$work/whole.a" --no-whole-archive

"$READELF" -Ws "$work/product-whole.o" >"$work/whole.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* needed$' "$work/whole.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* unused_member$' "$work/whole.symbols"

"$LD" -melf64lriscv -r -o "$work/reference-group.o" \
  "$work/root.o" --start-group "$work/group.a" --end-group
"$MINILD" -melf64lriscv -r -o "$work/product-group.o" \
  "$work/root.o" --start-group "$work/group.a" --end-group

"$READELF" -Ws "$work/product-group.o" >"$work/group.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* needed$' "$work/group.symbols"
if grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* unused_member$' "$work/group.symbols"; then
  echo "MINILD_A1_ERROR unused archive member was extracted" >&2
  exit 1
fi

"$LD" -melf64lriscv -Ttext=0x10000 -e root \
  "$work/reference-group.o" -o "$work/reference.elf"
"$LD" -melf64lriscv -Ttext=0x10000 -e root \
  "$work/product-group.o" -o "$work/product.elf"
"$OBJCOPY" -O binary --only-section=.text "$work/reference.elf" "$work/reference.text"
"$OBJCOPY" -O binary --only-section=.text "$work/product.elf" "$work/product.text"
cmp "$work/reference.text" "$work/product.text"

echo "MINILD_A1=PASS thin-whole=PASS archive-group-selection=PASS gnu-final-consumer=PASS"
