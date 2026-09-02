#!/bin/sh
set -eu

: "${MINIOBJCOPY:?MINIOBJCOPY must point to minic-objcopy}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
OBJCOPY=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}

work="$BUILD_DIR/tests/objcopy/a0"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.s" <<'EOF'
.section .text,"ax",@progbits
.globl _start
_start:
  .byte 0x11, 0x22, 0x33, 0x44
  ret

.section .note.test,"a",@note
.balign 4
.long 4
.long 4
.long 1
.asciz "MINI"
.balign 4
.long 0x12345678

.section .data,"aw",@progbits
.globl payload
payload:
  .byte 0xaa, 0xbb, 0xcc, 0xdd

.section .bss,"aw",@nobits
.globl zeroes
zeroes:
  .zero 32

.section .comment
.asciz "must-not-enter-binary"
EOF

cat >"$work/layout.lds" <<'EOF'
PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 0x10000;
  .text : AT(0x20000) { *(.text) } :text

  . = 0x10100;
  .note.test : AT(0x20100) { *(.note.test) } :text

  . = 0x11000;
  .data : AT(0x22000) { *(.data) } :data
  .bss : { *(.bss) } :data

  /DISCARD/ : { *(.riscv.attributes) }
}
EOF

"$AS" -march=rv64imac -mabi=lp64 -o "$work/input.o" "$work/input.s"
"$LD" -m elf64lriscv -T "$work/layout.lds" -o "$work/input.elf" "$work/input.o"
"$READELF" -lW "$work/input.elf" >"$work/programs.txt"

grep -q '0x0000000000020000' "$work/programs.txt"
grep -q '0x0000000000022000' "$work/programs.txt"

"$OBJCOPY" -O binary   -R .note.test   -R .comment   -S   "$work/input.elf" "$work/reference.bin"

"$MINIOBJCOPY" -O binary   -R .note.test   -R .comment   -S   "$work/input.elf" "$work/product.bin"

if ! cmp "$work/reference.bin" "$work/product.bin"; then
  echo "MINIOBJCOPY_A0_DIFF case=lma-gap-remove-strip" >&2
  cmp -l "$work/reference.bin" "$work/product.bin" | head -n 40 >&2 || true
  exit 1
fi

reference_size=$(stat -c %s "$work/reference.bin")
product_size=$(stat -c %s "$work/product.bin")
reference_hash=$(sha256sum "$work/reference.bin" | awk '{print $1}')
product_hash=$(sha256sum "$work/product.bin" | awk '{print $1}')

test "$reference_size" = "$product_size"
test "$reference_hash" = "$product_hash"

printf 'MINIOBJCOPY_A0=PASS oracle=GNU-objcopy format=ELF64-RISCV output=binary lma=PT_LOAD remove=2 strip=all bytes=%s sha256=%s\n'   "$product_size" "$product_hash"
