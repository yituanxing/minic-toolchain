#!/bin/sh
set -eu

: "${MINISTRIP:?MINISTRIP must point to minic-strip}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
STRIP=${RISCV_STRIP:-riscv64-linux-gnu-strip}
NM=${RISCV_NM:-riscv64-linux-gnu-nm}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}

work="$BUILD_DIR/tests/strip/m0"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.s" <<'EOF'
.text
.globl core_symbol
.type core_symbol, @function
core_symbol:
  call external_target
  ret
.size core_symbol, .-core_symbol

.local local_symbol
.type local_symbol, @function
local_symbol:
  ret
.size local_symbol, .-local_symbol

.data
.globl data_symbol
.type data_symbol, @object
data_symbol:
  .word 42
.size data_symbol, 4

.section .debug_info,"",@progbits
.4byte core_symbol
.4byte data_symbol
.byte 0xde, 0xad, 0xbe, 0xef

.section .debug_abbrev,"",@progbits
.byte 1, 2, 3, 4
EOF

cat >"$work/external.s" <<'EOF'
.text
.globl external_target
.type external_target, @function
external_target:
  ret
.size external_target, .-external_target
EOF

selected_symbols() {
  "$NM" -n "$1" |
    grep -E ' (core_symbol|local_symbol|data_symbol|external_target)$' || true
}

text_relocations() {
  "$READELF" -rW "$1" |
    grep -E 'external_target|Relocation section.*\.rela\.text' || true
}

run_case() {
  bits="$1"
  march="$2"
  abi="$3"
  emulation="$4"

  dir="$work/rv$bits"
  mkdir -p "$dir"

  "$AS" -march="$march" -mabi="$abi" -o "$dir/input.o" "$work/input.s"
  "$AS" -march="$march" -mabi="$abi" -o "$dir/external.o" "$work/external.s"

  cp "$dir/input.o" "$dir/gnu.o"
  cp "$dir/input.o" "$dir/mini.o"

  "$STRIP" --strip-debug "$dir/gnu.o"
  "$MINISTRIP" --strip-debug "$dir/mini.o"

  if "$READELF" -SW "$dir/mini.o" | grep -Eq '\.(debug|zdebug)'; then
    echo "MINISTRIP_M0_FAIL bits=$bits debug-section-remains" >&2
    exit 1
  fi
  if "$READELF" -SW "$dir/mini.o" | grep -q '\.rela\.debug'; then
    echo "MINISTRIP_M0_FAIL bits=$bits debug-relocation-remains" >&2
    exit 1
  fi
  "$READELF" -SW "$dir/mini.o" | grep -q '\.symtab'

  selected_symbols "$dir/gnu.o" >"$dir/gnu.sym"
  selected_symbols "$dir/mini.o" >"$dir/mini.sym"
  if ! cmp "$dir/gnu.sym" "$dir/mini.sym"; then
    echo "MINISTRIP_M0_DIFF bits=$bits phase=symbols" >&2
    diff -u "$dir/gnu.sym" "$dir/mini.sym" >&2 || true
    exit 1
  fi

  text_relocations "$dir/gnu.o" >"$dir/gnu.reloc"
  text_relocations "$dir/mini.o" >"$dir/mini.reloc"
  if ! cmp "$dir/gnu.reloc" "$dir/mini.reloc"; then
    echo "MINISTRIP_M0_DIFF bits=$bits phase=text-relocations" >&2
    diff -u "$dir/gnu.reloc" "$dir/mini.reloc" >&2 || true
    exit 1
  fi

  "$READELF" -aW "$dir/mini.o" >/dev/null 2>"$dir/mini.readelf.err"
  test ! -s "$dir/mini.readelf.err"

  "$LD" -m "$emulation" -r     -o "$dir/merged.gnu.o" "$dir/gnu.o" "$dir/external.o"
  "$LD" -m "$emulation" -r     -o "$dir/merged.mini.o" "$dir/mini.o" "$dir/external.o"
  selected_symbols "$dir/merged.gnu.o" >"$dir/merged.gnu.sym"
  selected_symbols "$dir/merged.mini.o" >"$dir/merged.mini.sym"
  cmp "$dir/merged.gnu.sym" "$dir/merged.mini.sym"

  printf 'MINISTRIP_M0_CASE=PASS bits=%s strip-debug=PASS symtab=PASS reloc=PASS relink=PASS gnu_bytes=%s mini_bytes=%s\n'     "$bits" "$(stat -c %s "$dir/gnu.o")" "$(stat -c %s "$dir/mini.o")"
}

run_case 64 rv64imac lp64 elf64lriscv
run_case 32 rv32imac ilp32 elf32lriscv

echo "MINISTRIP_M0=PASS oracle=GNU-strip formats=ELF64-RISCV,ELF32-RISCV type=ET_REL option=--strip-debug"
