#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}
NM=${RISCV_NM:-riscv64-linux-gnu-nm}
QEMU=${QEMU_RISCV64:-qemu-riscv64}

work="$BUILD_DIR/tests/linker/a2"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/start.s" <<'EOF'
.section .text.start,"ax",@progbits
.globl _start
.type _start, @function
_start:
  call compute
  li a7, 93
  ecall
.size _start, .-_start
EOF

cat >"$work/compute.s" <<'EOF'
.text
.globl compute
.type compute, @function
compute:
  la t0, ptr_to_value
  ld t0, 0(t0)
  ld a0, 0(t0)
  addi a0, a0, 2
  ret
.size compute, .-compute

.data
.align 3
.globl stored_value
.type stored_value, @object
stored_value:
  .dword 40
.size stored_value, 8

.globl ptr_to_value
.type ptr_to_value, @object
ptr_to_value:
  .dword stored_value
.size ptr_to_value, 8

.bss
.align 3
.globl zero_slot
.type zero_slot, @object
zero_slot:
  .zero 8
.size zero_slot, 8
EOF

"$AS" -march=rv64gc -mabi=lp64d -o "$work/start.o" "$work/start.s"
"$AS" -march=rv64gc -mabi=lp64d -o "$work/compute.o" "$work/compute.s"

"$LD" -melf64lriscv -static -z norelro --build-id=sha1 \
  --orphan-handling=warn --strip-debug -e _start -o "$work/reference" \
  "$work/start.o" "$work/compute.o"
"$MINILD" -melf64lriscv -static -z norelro --build-id=sha1 \
  --orphan-handling=warn --strip-debug -e _start -o "$work/product" \
  "$work/start.o" "$work/compute.o"

"$READELF" -h "$work/product" >"$work/product.header"
"$READELF" -l "$work/product" >"$work/product.programs"
"$READELF" -S "$work/product" >"$work/product.sections"
"$READELF" -s "$work/product" >"$work/product.symbols"
"$NM" -n "$work/product" >"$work/product.nm"
cat "$work/product.header"
cat "$work/product.programs"
cat "$work/product.sections"
cat "$work/product.nm"
grep -q 'EXEC (Executable file)' "$work/product.header"
grep -q 'RISC-V' "$work/product.header"
test "$(grep -c ' LOAD ' "$work/product.programs")" -eq 2
grep -q ' .symtab ' "$work/product.sections"
grep -q ' .strtab ' "$work/product.sections"
grep -q ' _start"$QEMU" "$work/reference"
reference_rc=$?
"$QEMU" "$work/product"
product_rc=$?
set -e

echo "MINILD_A2_DIAG reference_rc=$reference_rc product_rc=$product_rc"
test "$reference_rc" -eq 42
test "$product_rc" -eq 42

echo "MINILD_A2=PASS et_exec=PASS pt_load=2 symtab=PASS nm=PASS call=PASS pcrel=PASS data64=PASS qemu_rc=$product_rc"
 "$work/product.nm"
grep -q ' compute"$QEMU" "$work/reference"
reference_rc=$?
"$QEMU" "$work/product"
product_rc=$?
set -e

echo "MINILD_A2_DIAG reference_rc=$reference_rc product_rc=$product_rc"
test "$reference_rc" -eq 42
test "$product_rc" -eq 42

echo "MINILD_A2=PASS et_exec=PASS pt_load=2 call=PASS pcrel=PASS data64=PASS qemu_rc=$product_rc"
 "$work/product.nm"
grep -q ' stored_value"$QEMU" "$work/reference"
reference_rc=$?
"$QEMU" "$work/product"
product_rc=$?
set -e

echo "MINILD_A2_DIAG reference_rc=$reference_rc product_rc=$product_rc"
test "$reference_rc" -eq 42
test "$product_rc" -eq 42

echo "MINILD_A2=PASS et_exec=PASS pt_load=2 call=PASS pcrel=PASS data64=PASS qemu_rc=$product_rc"
 "$work/product.nm"
grep -q ' zero_slot"$QEMU" "$work/reference"
reference_rc=$?
"$QEMU" "$work/product"
product_rc=$?
set -e

echo "MINILD_A2_DIAG reference_rc=$reference_rc product_rc=$product_rc"
test "$reference_rc" -eq 42
test "$product_rc" -eq 42

echo "MINILD_A2=PASS et_exec=PASS pt_load=2 call=PASS pcrel=PASS data64=PASS qemu_rc=$product_rc"
 "$work/product.nm"

set +e
"$QEMU" "$work/reference"
reference_rc=$?
"$QEMU" "$work/product"
product_rc=$?
set -e

echo "MINILD_A2_DIAG reference_rc=$reference_rc product_rc=$product_rc"
test "$reference_rc" -eq 42
test "$product_rc" -eq 42

echo "MINILD_A2=PASS et_exec=PASS pt_load=2 call=PASS pcrel=PASS data64=PASS qemu_rc=$product_rc"
