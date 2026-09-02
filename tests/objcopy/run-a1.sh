#!/bin/sh
set -eu

: "${MINIOBJCOPY:?MINIOBJCOPY must point to minic-objcopy}"
: "${BUILD_DIR:?BUILD_DIR must be set}"

AS=${RISCV_AS:-riscv64-linux-gnu-as}
LD=${RISCV_LD:-riscv64-linux-gnu-ld}
OBJCOPY=${RISCV_OBJCOPY:-riscv64-linux-gnu-objcopy}
NM=${RISCV_NM:-riscv64-linux-gnu-nm}
READELF=${RISCV_READELF:-riscv64-linux-gnu-readelf}

work="$BUILD_DIR/tests/objcopy/a1"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/input.s" <<'EOF'
.text
.globl keep_me
.type keep_me, @function
keep_me:
  ret
.size keep_me, .-keep_me

.globl hide_me
.type hide_me, @function
hide_me:
  ret
.size hide_me, .-hide_me

.weak weak_me
.type weak_me, @function
weak_me:
  ret
.size weak_me, .-weak_me

.local local_me
.type local_me, @function
local_me:
  ret
.size local_me, .-local_me

.data
.globl data_me
.type data_me, @object
data_me:
  .word 42
.size data_me, 4

.section .debug_info,"",@progbits
.byte 0xde, 0xad, 0xbe, 0xef

.section .comment,"MS",@progbits,1
.asciz "miniobjcopy-a1"
EOF

normalize_selected_symbols() {
  "$NM" "$1" |
    grep -E ' (keep_me|hide_me|weak_me|local_me|data_me)$' || true
}

normalize_dynamic_symbols() {
  "$NM" -D -n "$1" 2>/dev/null || true
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
  "$LD" -m "$emulation" -shared -soname=miniobjcopy-a1.so     -o "$case_dir/input.so" "$case_dir/input.o"

  "$OBJCOPY" -G keep_me "$case_dir/input.so" "$case_dir/gnu.keep.so"
  "$MINIOBJCOPY" -G keep_me "$case_dir/input.so" "$case_dir/mini.keep.so"

  normalize_selected_symbols "$case_dir/gnu.keep.so" >"$case_dir/gnu.keep.sym"
  normalize_selected_symbols "$case_dir/mini.keep.so" >"$case_dir/mini.keep.sym"
  if ! cmp "$case_dir/gnu.keep.sym" "$case_dir/mini.keep.sym"; then
    echo "MINIOBJCOPY_A1_DIFF bits=$bits phase=keep-global-symbol" >&2
    diff -u "$case_dir/gnu.keep.sym" "$case_dir/mini.keep.sym" >&2 || true
    exit 1
  fi
  normalize_dynamic_symbols "$case_dir/gnu.keep.so" >"$case_dir/gnu.keep.dyn"
  normalize_dynamic_symbols "$case_dir/mini.keep.so" >"$case_dir/mini.keep.dyn"
  cmp "$case_dir/gnu.keep.dyn" "$case_dir/mini.keep.dyn"

  "$OBJCOPY" -S "$case_dir/gnu.keep.so" "$case_dir/gnu.strip.so"
  "$MINIOBJCOPY" -S "$case_dir/mini.keep.so" "$case_dir/mini.strip.so"

  normalize_dynamic_symbols "$case_dir/gnu.strip.so" >"$case_dir/gnu.strip.dyn"
  normalize_dynamic_symbols "$case_dir/mini.strip.so" >"$case_dir/mini.strip.dyn"
  cmp "$case_dir/gnu.strip.dyn" "$case_dir/mini.strip.dyn"
  normalize_program_headers "$case_dir/gnu.strip.so" >"$case_dir/gnu.strip.ph"
  normalize_program_headers "$case_dir/mini.strip.so" >"$case_dir/mini.strip.ph"
  if ! cmp "$case_dir/gnu.strip.ph" "$case_dir/mini.strip.ph"; then
    echo "MINIOBJCOPY_A1_DIFF bits=$bits phase=strip-program-headers" >&2
    diff -u "$case_dir/gnu.strip.ph" "$case_dir/mini.strip.ph" >&2 || true
    exit 1
  fi
  if "$READELF" -SW "$case_dir/mini.strip.so" | grep -q '\.symtab'; then
    echo "MINIOBJCOPY_A1_FAIL bits=$bits strip-left-symtab" >&2
    exit 1
  fi
  if "$READELF" -SW "$case_dir/mini.strip.so" | grep -q '\.debug_info'; then
    echo "MINIOBJCOPY_A1_FAIL bits=$bits strip-left-debug" >&2
    exit 1
  fi
  assert_readelf_clean "$case_dir/mini.strip.so"

  "$OBJCOPY" --strip-debug "$case_dir/input.so" "$case_dir/gnu.debug.so"
  "$MINIOBJCOPY" --strip-debug "$case_dir/input.so" "$case_dir/mini.debug.so"
  if "$READELF" -SW "$case_dir/mini.debug.so" | grep -q '\.debug_info'; then
    echo "MINIOBJCOPY_A1_FAIL bits=$bits strip-debug-left-debug" >&2
    exit 1
  fi
  "$READELF" -SW "$case_dir/mini.debug.so" | grep -q '\.symtab'
  normalize_selected_symbols "$case_dir/gnu.debug.so" >"$case_dir/gnu.debug.sym"
  normalize_selected_symbols "$case_dir/mini.debug.so" >"$case_dir/mini.debug.sym"
  cmp "$case_dir/gnu.debug.sym" "$case_dir/mini.debug.sym"
  assert_readelf_clean "$case_dir/mini.debug.so"

  "$OBJCOPY" -R .comment "$case_dir/input.so" "$case_dir/gnu.remove.so"
  "$MINIOBJCOPY" -R .comment "$case_dir/input.so" "$case_dir/mini.remove.so"
  if "$READELF" -SW "$case_dir/mini.remove.so" | grep -q '\.comment'; then
    echo "MINIOBJCOPY_A1_FAIL bits=$bits remove-left-comment" >&2
    exit 1
  fi
  normalize_dynamic_symbols "$case_dir/gnu.remove.so" >"$case_dir/gnu.remove.dyn"
  normalize_dynamic_symbols "$case_dir/mini.remove.so" >"$case_dir/mini.remove.dyn"
  cmp "$case_dir/gnu.remove.dyn" "$case_dir/mini.remove.dyn"
  assert_readelf_clean "$case_dir/mini.remove.so"

  gnu_strip_size=$(stat -c %s "$case_dir/gnu.strip.so")
  mini_strip_size=$(stat -c %s "$case_dir/mini.strip.so")
  printf 'MINIOBJCOPY_A1_CASE=PASS bits=%s keep-global=PASS strip-all=PASS strip-debug=PASS remove-section=PASS gnu_strip_bytes=%s mini_strip_bytes=%s\n'     "$bits" "$gnu_strip_size" "$mini_strip_size"
}

run_case 64 rv64imac lp64 elf64lriscv
run_case 32 rv32imac ilp32 elf32lriscv

echo "MINIOBJCOPY_A1=PASS oracle=GNU-objcopy formats=ELF64-RISCV,ELF32-RISCV types=ET_DYN options=-G,-S,--strip-debug,-R"
