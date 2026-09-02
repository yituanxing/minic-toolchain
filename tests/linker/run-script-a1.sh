#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
NM=${RISCV_NM:-riscv64-linux-gnu-nm}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}
QEMU=${QEMU_RISCV64:-qemu-riscv64}

work="$BUILD_DIR/tests/linker/script-a1"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/start.s" <<'EOF'
.section .text.start,"ax",@progbits
.globl _start
.type _start, @function
_start:
  li a0, 42
  li a7, 93
  ecall
.size _start, .-_start

.section .text.foo,"ax",@progbits
.globl foo
.type foo, @function
foo:
  ret
.size foo, .-foo

.section .init.text,"ax",@progbits
.globl init_foo
.type init_foo, @function
init_foo:
  ret
.size init_foo, .-init_foo

.section .data,"aw",@progbits
.globl data_word
.type data_word, @object
data_word:
  .dword 7
.size data_word, 8
EOF

cat >"$work/test.lds" <<'EOF'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : {
    _stext = .;
    *(.text.start)
    *(.text .text.*)
    _etext = .;
  }
  . = ALIGN(0x1000);
  .init.text : {
    _sinittext = .;
    *(.init.text .init.text.*)
    _einittext = .;
  }
  . = ALIGN(0x1000);
  .data : {
    _data = .;
    *(.data)
  }
}
EOF

"$AS" -march=rv64imac -mabi=lp64 -o "$work/start.o" "$work/start.s"
"$MINILD" -melf64lriscv -static --script="$work/test.lds"   -o "$work/product" "$work/start.o"

"$NM" -n "$work/product" >"$work/product.nm"
"$READELF" -h "$work/product" >"$work/product.header"

symbol_type() {
  awk -v symbol="$1" '$3 == symbol { print $2; exit }' "$work/product.nm"
}

test "$(symbol_type _stext)" = T
test "$(symbol_type _etext)" = T
test "$(symbol_type _sinittext)" = T
test "$(symbol_type _einittext)" = T
test "$(symbol_type _data)" = D
grep -q 'Entry point address:.*0x10000' "$work/product.header"

set +e
"$QEMU" "$work/product"
product_rc=$?
set -e

test "$product_rc" -eq 42
echo "MINILD_SCRIPT_A1=PASS output-symbol-section=PASS nm-types=T/T/T/T,D entry=0x10000 qemu_rc=$product_rc"
