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

# GNU ld aligns a script-directed output section before evaluating its
# leading __start___param symbol. Linux boot dereferences this address as
# an array of 40-byte kernel_param records. A two-byte early boundary
# yields a bogus pointer and an early page fault in parse_args().
cat >"$work/param.s" <<'EOF'
.section .text,"ax",@progbits
.globl _start
_start:
  ret
.section __param,"a",@progbits
.balign 8
.dword _start
.dword 0x1122334455667788
EOF
cat >"$work/param.lds" <<'EOF'
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text) }
  . = 0x11002;
  __param : {
    __start___param = .;
    KEEP(*(__param))
    __stop___param = .;
  }
}
EOF
"$AS" -march=rv64imac -mabi=lp64 -o "$work/param.o" "$work/param.s"
"$LD" -melf64lriscv -static -T "$work/param.lds" -o "$work/param-gnu" "$work/param.o"
"$MINILD" -melf64lriscv -static -T "$work/param.lds" -o "$work/param-mini" "$work/param.o"
"$NM" "$work/param-gnu" >"$work/param-gnu.nm"
"$NM" "$work/param-mini" >"$work/param-mini.nm"
for sym in __start___param __stop___param; do
  gnu_addr=$(awk -v sym="$sym" '$3==sym {print $1; exit}' "$work/param-gnu.nm")
  mini_addr=$(awk -v sym="$sym" '$3==sym {print $1; exit}' "$work/param-mini.nm")
  test -n "$gnu_addr"
  test "$mini_addr" = "$gnu_addr"
done
start_addr=$(awk '$3=="__start___param" {print $1; exit}' "$work/param-mini.nm")
test "$start_addr" = 0000000000011008
"$OBJCOPY" --dump-section __param="$work/param-gnu.bin" "$work/param-gnu"
"$OBJCOPY" --dump-section __param="$work/param-mini.bin" "$work/param-mini"
cmp "$work/param-gnu.bin" "$work/param-mini.bin"
test "$(wc -c < "$work/param-mini.bin")" -eq 16

echo "MINILD_SCRIPT_A1=PASS output-symbol-section=PASS nm-types=T/T/T/T,D entry=0x10000 loads=$load_count generated=gnu-exact-504-bytes param-start=gnu-exact-0x11008 consumers=nm,readelf,objcopy"
