#!/bin/sh
set -eu

: "${MINIOBJCOPY:?MINIOBJCOPY must point to minic-objcopy}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
OBJCOPY=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}
NM=${RISCV_NM:-riscv64-linux-gnu-nm}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}

work="$BUILD_DIR/tests/objcopy/a2"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.s" <<'EOF'
.text
.globl early_entry
.type early_entry, @function
early_entry:
  call external_target
  ret
.size early_entry, .-early_entry

.section .rodata,"a",@progbits
.globl early_rodata
.type early_rodata, @object
early_rodata:
  .word 7
.size early_rodata, 4

.data
.globl early_data
.type early_data, @object
early_data:
  .word 42
.size early_data, 4

.bss
.globl early_bss
.type early_bss, @object
early_bss:
  .zero 8
.size early_bss, 8

.section .note.gnu.property,"",@note
.align 3
.quad 0
EOF

"$AS" -march=rv64imac -mabi=lp64 -o "$work/input.o" "$work/input.s"

"$OBJCOPY"   --prefix-symbols=__pi_   --remove-section=.note.gnu.property   --prefix-alloc-sections=.init.pi   "$work/input.o" "$work/gnu.o"

"$MINIOBJCOPY"   --prefix-symbols=__pi_   --remove-section=.note.gnu.property   --prefix-alloc-sections=.init.pi   "$work/input.o" "$work/mini.o"

normalize_symbols() {
  LC_ALL=C "$NM" -n "$1" |
    grep -E ' (__pi_early_entry|__pi_early_rodata|__pi_early_data|__pi_early_bss|__pi_external_target)$' ||
    true
}

normalize_alloc_sections() {
  "$READELF" -SW "$1" |
    awk '$0 ~ /\.init\.pi/ {print $3}' |
    sort
}

normalize_external_reloc() {
  "$READELF" -rW "$1" |
    grep '__pi_external_target' |
    sed -E 's/^[[:space:]]*[0-9a-fA-F]+[[:space:]]+[0-9a-fA-F]+[[:space:]]+//' ||
    true
}

normalize_symbols "$work/gnu.o" >"$work/gnu.sym"
normalize_symbols "$work/mini.o" >"$work/mini.sym"
if ! cmp "$work/gnu.sym" "$work/mini.sym"; then
  echo "MINIOBJCOPY_A2_DIFF phase=symbol-prefix" >&2
  diff -u "$work/gnu.sym" "$work/mini.sym" >&2 || true
  exit 1
fi

normalize_alloc_sections "$work/gnu.o" >"$work/gnu.sections"
normalize_alloc_sections "$work/mini.o" >"$work/mini.sections"
if ! cmp "$work/gnu.sections" "$work/mini.sections"; then
  echo "MINIOBJCOPY_A2_DIFF phase=alloc-section-prefix" >&2
  diff -u "$work/gnu.sections" "$work/mini.sections" >&2 || true
  exit 1
fi

if "$READELF" -SW "$work/mini.o" | grep -q '\.note\.gnu\.property'; then
  echo "MINIOBJCOPY_A2_FAIL remove-section=.note.gnu.property" >&2
  exit 1
fi

normalize_external_reloc "$work/gnu.o" >"$work/gnu.reloc"
normalize_external_reloc "$work/mini.o" >"$work/mini.reloc"
if ! cmp "$work/gnu.reloc" "$work/mini.reloc"; then
  echo "MINIOBJCOPY_A2_DIFF phase=relocation" >&2
  diff -u "$work/gnu.reloc" "$work/mini.reloc" >&2 || true
  exit 1
fi

"$READELF" -aW "$work/mini.o" >/dev/null 2>"$work/mini.readelf.err"
test ! -s "$work/mini.readelf.err"

"$LD" -m elf64lriscv -r -o "$work/gnu.merged.o" "$work/gnu.o"
"$LD" -m elf64lriscv -r -o "$work/mini.merged.o" "$work/mini.o"
normalize_symbols "$work/gnu.merged.o" >"$work/gnu.merged.sym"
normalize_symbols "$work/mini.merged.o" >"$work/mini.merged.sym"
cmp "$work/gnu.merged.sym" "$work/mini.merged.sym"

echo "MINIOBJCOPY_A2=PASS oracle=GNU-objcopy type=ET_REL options=prefix-symbols,remove-section,prefix-alloc-sections consumer=GNU-ld-r"
