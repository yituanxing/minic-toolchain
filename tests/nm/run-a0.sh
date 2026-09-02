#!/bin/sh
set -eu

: "${MININM:?MININM must point to minic-nm}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
NM=${RISCV_NM:-riscv64-linux-gnu-nm}

work="$BUILD_DIR/tests/nm/a0"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.s" <<'EOF'
.text
.globl text_global
.type text_global, @function
text_global:
  call undefined_target
  ret
.size text_global, .-text_global

.local text_local
.type text_local, @function
text_local:
  ret
.size text_local, .-text_local

.weak weak_text
.type weak_text, @function
weak_text:
  ret
.size weak_text, .-weak_text

.section .rodata,"a",@progbits
.globl ro_global
.type ro_global, @object
ro_global:
  .word 11
.size ro_global, 4

.data
.globl data_global
.type data_global, @object
data_global:
  .word 22
.size data_global, 4

.local data_local
.type data_local, @object
data_local:
  .word 33
.size data_local, 4

.weak weak_undefined
.word weak_undefined

.bss
.globl bss_global
.type bss_global, @object
bss_global:
  .zero 8
.size bss_global, 8

.comm common_global,8,8

.globl absolute_global
.set absolute_global, 0x1234
EOF

cat >"$work/image.s" <<'EOF'
.text
.globl _start
.type _start, @function
_start:
  ret
.size _start, .-_start

.globl image_text
.type image_text, @function
image_text:
  ret
.size image_text, .-image_text

.section .rodata,"a",@progbits
.globl image_ro
.type image_ro, @object
image_ro:
  .word 7
.size image_ro, 4

.data
.globl image_data
.type image_data, @object
image_data:
  .word 9
.size image_data, 4

.bss
.globl image_bss
.type image_bss, @object
image_bss:
  .zero 4
.size image_bss, 4
EOF

"$AS" -march=rv64imac -mabi=lp64 -o "$work/input64.o" "$work/input.s"
"$AS" -march=rv32imac -mabi=ilp32 -o "$work/input32.o" "$work/input.s"
"$AS" -march=rv64imac -mabi=lp64 -o "$work/image64.o" "$work/image.s"
"$LD" -m elf64lriscv -e _start -o "$work/image64.elf" "$work/image64.o"
"$LD" -m elf64lriscv -shared -o "$work/image64.so" "$work/image64.o"

symbols='absolute_global|bss_global|common_global|data_global|data_local|ro_global|text_global|text_local|undefined_target|weak_text|weak_undefined'
image_symbols='_start|image_bss|image_data|image_ro|image_text'

filter_symbols() {
  grep -E " ($2)$" "$1" || true
}

compare_case() {
  label="$1"
  file="$2"
  pattern="$3"
  shift 3
  LC_ALL=C "$NM" "$@" "$file" >"$work/$label.gnu.all"
  LC_ALL=C "$MININM" "$@" "$file" >"$work/$label.mini.all"
  filter_symbols "$work/$label.gnu.all" "$pattern" >"$work/$label.gnu"
  filter_symbols "$work/$label.mini.all" "$pattern" >"$work/$label.mini"
  if ! cmp "$work/$label.gnu" "$work/$label.mini"; then
    echo "MININM_A0_DIFF case=$label" >&2
    diff -u "$work/$label.gnu" "$work/$label.mini" >&2 || true
    exit 1
  fi
  echo "MININM_A0_CASE=PASS case=$label"
}

for bits in 64 32; do
  object="$work/input$bits.o"
  compare_case "rv$bits-default" "$object" "$symbols"
  compare_case "rv$bits-numeric" "$object" "$symbols" -n
  compare_case "rv$bits-global" "$object" "$symbols" -g
  compare_case "rv$bits-undefined" "$object" "$symbols" -u
  compare_case "rv$bits-defined" "$object" "$symbols" --defined-only
  compare_case "rv$bits-nosort" "$object" "$symbols" -p
done

compare_case "rv64-exec" "$work/image64.elf" "$image_symbols"
compare_case "rv64-dyn" "$work/image64.so" "$image_symbols"

echo "MININM_A0=PASS oracle=GNU-nm formats=ELF32,ELF64 types=REL,EXEC,DYN options=default,-n,-g,-u,--defined-only,-p"
