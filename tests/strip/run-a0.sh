#!/bin/sh
set -eu

: "${MINISTRIP:?MINISTRIP must point to minic-strip}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
STRIP=${RISCV_STRIP:-riscv64-linux-gnu-strip}
NM=${RISCV_NM:-riscv64-linux-gnu-nm}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}

work="$BUILD_DIR/tests/strip/a0"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.s" <<'EOF'
.text
.globl exported_fn
.type exported_fn, @function
exported_fn:
  ret
.size exported_fn, .-exported_fn

.globl hidden_fn
.type hidden_fn, @function
hidden_fn:
  ret
.size hidden_fn, .-hidden_fn

.data
.globl exported_data
.type exported_data, @object
exported_data:
  .word 123
.size exported_data, 4

.section .debug_info,"",@progbits
.byte 0xde, 0xad, 0xbe, 0xef

.section .comment,"MS",@progbits,1
.asciz "minic-strip-a0"
EOF

normalize_dynamic_symbols() {
  "$NM" -D -n "$1" 2>/dev/null || true
}

normalize_static_symbols() {
  "$NM" -n "$1" 2>/dev/null |
    grep -E ' (exported_fn|hidden_fn|exported_data)$' || true
}

normalize_program_headers() {
  "$READELF" -lW "$1" |
    awk '/^  (LOAD|DYNAMIC|NOTE|GNU_|TLS)/ {print}'
}

assert_readelf_clean() {
  "$READELF" -aW "$1" >/dev/null 2>"$1.readelf.err"
  if test -s "$1.readelf.err"; then
    cat "$1.readelf.err" >&2
    return 1
  fi
}

run_case() {
  bits="$1"
  march="$2"
  abi="$3"
  emulation="$4"

  case_dir="$work/rv$bits"
  mkdir -p "$case_dir"

  "$AS" -march="$march" -mabi="$abi"     -o "$case_dir/input.o" "$work/input.s"
  "$LD" -m "$emulation" -shared -soname=minic-strip-a0.so     -o "$case_dir/input.so" "$case_dir/input.o"

  chmod 0755 "$case_dir/input.so"

  "$STRIP" -s -o "$case_dir/gnu.all.so" "$case_dir/input.so"
  "$MINISTRIP" -s -o "$case_dir/mini.all.so" "$case_dir/input.so"

  if "$READELF" -SW "$case_dir/mini.all.so" | grep -q '\.symtab'; then
    echo "MINISTRIP_A0_FAIL bits=$bits strip-all-left-symtab" >&2
    exit 1
  fi
  if "$READELF" -SW "$case_dir/mini.all.so" | grep -q '\.debug_info'; then
    echo "MINISTRIP_A0_FAIL bits=$bits strip-all-left-debug" >&2
    exit 1
  fi
  test "$(stat -c %a "$case_dir/mini.all.so")" = 755

  normalize_dynamic_symbols "$case_dir/gnu.all.so" >"$case_dir/gnu.all.dyn"
  normalize_dynamic_symbols "$case_dir/mini.all.so" >"$case_dir/mini.all.dyn"
  cmp "$case_dir/gnu.all.dyn" "$case_dir/mini.all.dyn"

  normalize_program_headers "$case_dir/gnu.all.so" >"$case_dir/gnu.all.ph"
  normalize_program_headers "$case_dir/mini.all.so" >"$case_dir/mini.all.ph"
  cmp "$case_dir/gnu.all.ph" "$case_dir/mini.all.ph"
  assert_readelf_clean "$case_dir/mini.all.so"

  "$STRIP" -g -o "$case_dir/gnu.debug.so" "$case_dir/input.so"
  "$MINISTRIP" -g -o "$case_dir/mini.debug.so" "$case_dir/input.so"

  if "$READELF" -SW "$case_dir/mini.debug.so" | grep -q '\.debug_info'; then
    echo "MINISTRIP_A0_FAIL bits=$bits strip-debug-left-debug" >&2
    exit 1
  fi
  "$READELF" -SW "$case_dir/mini.debug.so" | grep -q '\.symtab'

  normalize_static_symbols "$case_dir/gnu.debug.so" >"$case_dir/gnu.debug.sym"
  normalize_static_symbols "$case_dir/mini.debug.so" >"$case_dir/mini.debug.sym"
  cmp "$case_dir/gnu.debug.sym" "$case_dir/mini.debug.sym"
  assert_readelf_clean "$case_dir/mini.debug.so"

  cp "$case_dir/input.so" "$case_dir/gnu.inplace.so"
  cp "$case_dir/input.so" "$case_dir/mini.inplace.so"
  chmod 0751 "$case_dir/mini.inplace.so"

  "$STRIP" "$case_dir/gnu.inplace.so"
  "$MINISTRIP" "$case_dir/mini.inplace.so"

  if "$READELF" -SW "$case_dir/mini.inplace.so" | grep -q '\.symtab'; then
    echo "MINISTRIP_A0_FAIL bits=$bits inplace-left-symtab" >&2
    exit 1
  fi
  test "$(stat -c %a "$case_dir/mini.inplace.so")" = 751

  normalize_dynamic_symbols "$case_dir/gnu.inplace.so" >"$case_dir/gnu.inplace.dyn"
  normalize_dynamic_symbols "$case_dir/mini.inplace.so" >"$case_dir/mini.inplace.dyn"
  cmp "$case_dir/gnu.inplace.dyn" "$case_dir/mini.inplace.dyn"
  normalize_program_headers "$case_dir/gnu.inplace.so" >"$case_dir/gnu.inplace.ph"
  normalize_program_headers "$case_dir/mini.inplace.so" >"$case_dir/mini.inplace.ph"
  cmp "$case_dir/gnu.inplace.ph" "$case_dir/mini.inplace.ph"
  assert_readelf_clean "$case_dir/mini.inplace.so"

  printf 'MINISTRIP_A0_CASE=PASS bits=%s strip-all=PASS strip-debug=PASS inplace=PASS output-mode=PASS\n' "$bits"
}

run_case 64 rv64imac lp64 elf64lriscv
run_case 32 rv32imac ilp32 elf32lriscv

echo "MINISTRIP_A0=PASS oracle=GNU-strip formats=ELF64-RISCV,ELF32-RISCV types=ET_DYN options=default,-s,-g,-o"
