#!/bin/sh
set -eu

: "${MINILD:?MINILD must point to minic-ld}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}

work="$BUILD_DIR/tests/linker/a4"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/shared.s" <<'EOF'
.text
.globl exported_answer
.type exported_answer, @function
exported_answer:
  li a0, 42
  ret
.size exported_answer, .-exported_answer

.data
.align 3
.local local_value
.type local_value, @object
local_value:
  .dword 7
.size local_value, 8

.globl exported_ptr
.type exported_ptr, @object
exported_ptr:
  .dword local_value
.size exported_ptr, 8

.globl external_ptr
.type external_ptr, @object
external_ptr:
  .dword external_value
.size external_ptr, 8
EOF

"$AS" -march=rv64gc -mabi=lp64d -o "$work/shared.o" "$work/shared.s"

"$LD" -melf64lriscv -shared -soname libminild-a4.so \
  -o "$work/reference.so" "$work/shared.o"

"$MINILD" -melf64lriscv -shared -soname libminild-a4.so \
  -o "$work/product.so" "$work/shared.o"

"$READELF" -h "$work/product.so" >"$work/product.header"
"$READELF" -l "$work/product.so" >"$work/product.programs"
"$READELF" -S "$work/product.so" >"$work/product.sections"
"$READELF" -d "$work/product.so" >"$work/product.dynamic"
"$READELF" -Ws "$work/product.so" >"$work/product.symbols"
"$READELF" -Wr "$work/product.so" >"$work/product.relocs"

grep -q 'DYN (Shared object file)' "$work/product.header"
grep -q 'RISC-V' "$work/product.header"
grep -q ' DYNAMIC ' "$work/product.programs"
grep -Eq '] \.dynstr[[:space:]]+STRTAB' "$work/product.sections"
grep -Eq '] \.dynsym[[:space:]]+DYNSYM' "$work/product.sections"
grep -Eq '] \.hash[[:space:]]+HASH' "$work/product.sections"
grep -Eq '] \.rela\.dyn[[:space:]]+RELA' "$work/product.sections"
grep -Eq '] \.dynamic[[:space:]]+DYNAMIC' "$work/product.sections"
grep -q '(SONAME).*\[libminild-a4.so\]' "$work/product.dynamic"
grep -q '(HASH)' "$work/product.dynamic"
grep -q '(STRTAB)' "$work/product.dynamic"
grep -q '(SYMTAB)' "$work/product.dynamic"
grep -q '(RELA)' "$work/product.dynamic"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* exported_answer$' "$work/product.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* exported_ptr$' "$work/product.symbols"
grep -Eq 'GLOBAL[[:space:]]+DEFAULT.* external_value$' "$work/product.symbols"
grep -q 'R_RISCV_RELATIVE' "$work/product.relocs"
grep -Eq 'R_RISCV_64.*external_value' "$work/product.relocs"

reference_relative="$(grep -c 'R_RISCV_RELATIVE' "$work/reference.so" 2>/dev/null || true)"
product_relative="$(grep -c 'R_RISCV_RELATIVE' "$work/product.relocs")"
test "$product_relative" -ge 1

echo "MINILD_A4=PASS et_dyn=PASS dynsym=PASS dynamic=PASS relative=PASS external64=PASS soname=PASS"
