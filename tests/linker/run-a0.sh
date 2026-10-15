#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}
OBJCOPY=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}

work="$BUILD_DIR/tests/linker/a0"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/a.s" <<'EOF'
.text
.globl alpha
.type alpha, @function
alpha:
  call beta
  ret
.size alpha, .-alpha

.data
.globl ptr_to_beta
.type ptr_to_beta, @object
ptr_to_beta:
  .dword beta
.size ptr_to_beta, 8
EOF

cat >"$work/b.s" <<'EOF'
.text
.globl beta
.type beta, @function
beta:
  li a0, 42
  ret
.size beta, .-beta

.section .rodata,"a"
.globl answer
.type answer, @object
answer:
  .word 42
.size answer, 4
EOF

"$AS" -march=rv64gc -mabi=lp64d -o "$work/a.o" "$work/a.s"
"$AS" -march=rv64gc -mabi=lp64d -o "$work/b.o" "$work/b.s"

"$LD" -melf64lriscv -z noexecstack --no-warn-rwx-segments \
  -r -o "$work/reference.o" "$work/a.o" "$work/b.o"

"$MINILD" -melf64lriscv -z noexecstack --no-warn-rwx-segments \
  -r -o "$work/product.o" "$work/a.o" "$work/b.o"

"$READELF" -h "$work/product.o" | grep -q 'REL (Relocatable file)'
"$READELF" -h "$work/product.o" | grep -q 'RISC-V'
"$READELF" -SW "$work/product.o" >"$work/product.sections"
grep -Eq '] \.text[[:space:]]+PROGBITS' "$work/product.sections"
grep -Eq '] \.data[[:space:]]+PROGBITS' "$work/product.sections"
grep -Eq '] \.rodata[[:space:]]+PROGBITS' "$work/product.sections"
grep -Eq '] \.rela\.text[[:space:]]+RELA' "$work/product.sections"
grep -Eq '] \.rela\.data[[:space:]]+RELA' "$work/product.sections"

"$READELF" -Ws "$work/product.o" >"$work/product.symbols"
test "$(
  awk '$8=="beta" {count++; if ($7!="UND") defined++} END {print count+0 ":" defined+0}' \
    "$work/product.symbols"
)" = "1:1"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* alpha$' "$work/product.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* ptr_to_beta$' "$work/product.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* answer$' "$work/product.symbols"

"$READELF" -Wr "$work/product.o" >"$work/product.relocs"
grep -q 'R_RISCV_CALL_PLT.*beta' "$work/product.relocs"
grep -q 'R_RISCV_64.*beta' "$work/product.relocs"

"$LD" -melf64lriscv -Ttext=0x10000 -e alpha \
  "$work/reference.o" -o "$work/reference.elf"
"$LD" -melf64lriscv -Ttext=0x10000 -e alpha \
  "$work/product.o" -o "$work/product.elf"

"$OBJCOPY" -O binary --only-section=.text "$work/reference.elf" "$work/reference.text"
"$OBJCOPY" -O binary --only-section=.text "$work/product.elf" "$work/product.text"
cmp "$work/reference.text" "$work/product.text"

echo "MINILD_A0=PASS et_rel=PASS symbols=PASS rela=PASS gnu-final-consumer=PASS"
