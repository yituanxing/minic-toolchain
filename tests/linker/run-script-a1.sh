#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
NM=${RISCV_NM:-riscv64-linux-gnu-nm}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
OBJCOPY=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}
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
  .generated : { BYTE(0x5a); . = ALIGN(0x200); }
}
EOF

"$AS" -march=rv64imac -mabi=lp64 -o "$work/start.o" "$work/start.s"
"$MINILD" -melf64lriscv -static --script="$work/test.lds"   -o "$work/product" "$work/start.o"
"$LD" -melf64lriscv -static --script="$work/test.lds" -o "$work/reference" "$work/start.o"
"$OBJCOPY" --dump-section .generated="$work/generated-product.bin" "$work/product"
"$OBJCOPY" --dump-section .generated="$work/generated-reference.bin" "$work/reference"
cmp "$work/generated-reference.bin" "$work/generated-product.bin"
test "$(wc -c <"$work/generated-product.bin")" -eq 504
test "$(od -An -tu1 -N1 "$work/generated-product.bin" | tr -d ' ')" = 90

"$NM" -n "$work/product" >"$work/product.nm"
"$READELF" -h "$work/product" >"$work/product.header"
"$READELF" -lW "$work/product" >"$work/product.programs"
load_count=$(awk '$1 == "LOAD" { count++ } END { print count+0 }' "$work/product.programs")
# .text.start and .text.foo are adjacent inputs in one output class.
# The page-separated init text and data must remain separate PT_LOADs.
test "$load_count" -eq 4

symbol_type() {
  awk -v symbol="$1" '$3 == symbol { print $2; exit }' "$work/product.nm"
}

test "$(symbol_type _stext)" = T
test "$(symbol_type _etext)" = T
test "$(symbol_type _sinittext)" = T
test "$(symbol_type _einittext)" = T
test "$(symbol_type _data)" = D
grep -q 'Entry point address:.*0x10000' "$work/product.header"

echo "MINILD_SCRIPT_A1=PASS output-symbol-section=PASS nm-types=T/T/T/T,D entry=0x10000 loads=$load_count generated=gnu-exact-504-bytes consumers=nm,readelf,objcopy"
