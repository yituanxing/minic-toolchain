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

# Linux's GNU binutils may spell a thin long-name reference as
# "/offset + spaces + /". Convert one synthetic member to that 16-byte form.
cp "$work/whole.a" "$work/whole-slash.a"
python3 - "$work/whole-slash.a" <<'PY'
import sys
path = sys.argv[1]
data = bytearray(open(path, "rb").read())
cursor = 8
while cursor < len(data):
    header = data[cursor:cursor+60]
    if len(header) != 60 or header[58:60] != bytes((0x60, 0x0a)):
        raise SystemExit("bad archive header")
    name = bytes(header[:16])
    size = int(bytes(header[48:58]).decode().strip() or "0")
    cursor += 60
    stripped = name.rstrip(b" ")
    embedded = stripped in (b"/", b"//", b"/SYM64/")
    if stripped.startswith(b"/") and len(stripped) > 1 and stripped[1:2].isdigit():
        digits = stripped[1:].decode()
        prefix = ("/" + digits).encode()
        field = prefix + b" " * (15 - len(prefix)) + b"/"
        if len(field) != 16:
            raise SystemExit("bad slash-form field")
        data[cursor-60:cursor-44] = field
        break
    if embedded:
        cursor += size + (size & 1)
else:
    raise SystemExit("long-name reference not found")
open(path, "wb").write(data)
PY

"$MINILD" -melf64lriscv -r -o "$work/product-whole-slash.o" \
  --whole-archive "$work/whole-slash.a" --no-whole-archive
"$READELF" -Ws "$work/product-whole-slash.o" >"$work/whole-slash.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* needed$' "$work/whole-slash.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* unused_member$' "$work/whole-slash.symbols"

"$LD" -melf64lriscv -r -o "$work/reference-group.o" \
  --start-group "$work/root.o" "$work/group.a" --end-group
"$MINILD" -melf64lriscv -r -o "$work/product-group.o" \
  --start-group "$work/root.o" "$work/group.a" --end-group

"$READELF" -Ws "$work/product-group.o" >"$work/group.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* needed$' "$work/group.symbols"
if grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* unused_member$' "$work/group.symbols"; then
  echo "MINILD_A1_ERROR unused archive member was extracted" >&2
  exit 1
fi

cat >"$work/cross-root.s" <<'EOF'
.text
.globl cross_root
.type cross_root, @function
cross_root:
  tail late_entry
.size cross_root, .-cross_root
EOF

cat >"$work/lib/early.s" <<'EOF'
.text
.globl early_helper
.type early_helper, @function
early_helper:
  li a0, 23
  ret
.size early_helper, .-early_helper
EOF

cat >"$work/lib/late.s" <<'EOF'
.text
.globl late_entry
.type late_entry, @function
late_entry:
  tail early_helper
.size late_entry, .-late_entry
EOF

"$AS" -march=rv64gc -mabi=lp64d -o "$work/cross-root.o" "$work/cross-root.s"
"$AS" -march=rv64gc -mabi=lp64d -o "$work/lib/early.o" "$work/lib/early.s"
"$AS" -march=rv64gc -mabi=lp64d -o "$work/lib/late.o" "$work/lib/late.s"
(
  cd "$work"
  "$AR" rcsD early.a lib/early.o
  "$AR" rcsD late.a lib/late.o
)

"$LD" -melf64lriscv -r -o "$work/reference-cross-group.o" \
  "$work/cross-root.o" \
  --start-group "$work/early.a" "$work/late.a" --end-group
"$MINILD" -melf64lriscv -r -o "$work/product-cross-group.o" \
  "$work/cross-root.o" \
  --start-group "$work/early.a" "$work/late.a" --end-group

"$READELF" -Ws "$work/product-cross-group.o" >"$work/cross-group.symbols"
awk '$8 == "late_entry" && $7 != "UND" { found=1 } END { exit found ? 0 : 1 }' "$work/cross-group.symbols"
awk '$8 == "early_helper" && $7 != "UND" { found=1 } END { exit found ? 0 : 1 }' "$work/cross-group.symbols"
if awk '$8 == "early_helper" && $7 == "UND" { found=1 } END { exit found ? 0 : 1 }' "$work/cross-group.symbols"; then
  echo "MINILD_A1_ERROR group-wide rescan left early_helper undefined" >&2
  exit 1
fi

"$LD" -melf64lriscv -Ttext=0x11000 -e cross_root \
  "$work/product-cross-group.o" -o "$work/product-cross-group.elf"

# BusyBox-shaped group: an archive appears before a plain object.  The object
# introduces a new unresolved symbol, so the earlier archive must be rescanned.
"$LD" -melf64lriscv -r -o "$work/reference-object-after-archive.o" \
  "$work/cross-root.o" \
  --start-group "$work/early.a" "$work/lib/late.o" --end-group
"$MINILD" -melf64lriscv -r -o "$work/product-object-after-archive.o" \
  "$work/cross-root.o" \
  --start-group "$work/early.a" "$work/lib/late.o" --end-group

"$READELF" -Ws "$work/product-object-after-archive.o" \
  >"$work/object-after-archive.symbols"
awk '$8 == "early_helper" && $7 != "UND" { found=1 } END { exit found ? 0 : 1 }' \
  "$work/object-after-archive.symbols"
if awk '$8 == "early_helper" && $7 == "UND" { found=1 } END { exit found ? 0 : 1 }' \
  "$work/object-after-archive.symbols"; then
  echo "MINILD_A1_ERROR object-after-archive group rescan failed" >&2
  exit 1
fi

"$LD" -melf64lriscv -Ttext=0x10000 -e root \
  "$work/reference-group.o" -o "$work/reference.elf"
"$LD" -melf64lriscv -Ttext=0x10000 -e root \
  "$work/product-group.o" -o "$work/product.elf"
"$OBJCOPY" -O binary --only-section=.text "$work/reference.elf" "$work/reference.text"
"$OBJCOPY" -O binary --only-section=.text "$work/product.elf" "$work/product.text"
cmp "$work/reference.text" "$work/product.text"

echo "MINILD_A1=PASS thin-whole=PASS long-name-slash=PASS archive-group-selection=PASS group-rescan=PASS object-inside-group=PASS object-after-archive=PASS gnu-final-consumer=PASS"
