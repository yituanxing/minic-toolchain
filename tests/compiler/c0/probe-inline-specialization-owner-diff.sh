#!/usr/bin/env bash
# Exact P38/P39 behavior gate for the three AST inline-specialization passes.
# Both compilers consume the *same* host-preprocessed C; success compares the
# emitted assembly bytes, while two rejected inputs count as an unchanged
# acceptance boundary. This is not a replacement for GNU ELF/QEMU runtime.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
baseline=${MINIC_BASELINE_CC:?set MINIC_BASELINE_CC to the P38 compiler}
current=${MINIC_CURRENT_CC:-"$repo_root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
test -x "$baseline"
test -x "$current"
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT

fixtures=(
  inline_function
  inline_symbolic_shared_body
  symbolic_inline_two_callers
  mc001_symbolic_closure_arena
  core_inline_statement_constant_guard
  core_cfg_fold_effectful_boolean_range
  gnu_linux_ioremap_inline_arg_guard
  gnu_linux_kmalloc_index_bool_guard
  gnu_extern_inline_attribute
  gnu_inline_keyword_spellings
  core_opaque_inline_asm
  gnu_inline_asm_named_operands
  gnu_inline_asm_satp_direct
  gnu_inline_asm_satp_window
  gnu_inline_asm_readwrite_output
  gnu_inline_asm_goto
)
targets=(riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux)

identical=0
both_reject=0
different=0
for fixture in "${fixtures[@]}"; do
  "$host_cc" -E -P -std=gnu11 -I"$repo_root/include" \
    -I"$repo_root/tests/compiler/c0" \
    -x c "$repo_root/tests/compiler/c0/$fixture.c" \
    -o "$tmp_dir/$fixture.i"
  for target in "${targets[@]}"; do
    old_status=0
    new_status=0
    "$baseline" -S "$tmp_dir/$fixture.i" --target="$target" \
      -o "$tmp_dir/old.s" >"$tmp_dir/old.log" 2>&1 || old_status=$?
    "$current" -S "$tmp_dir/$fixture.i" --target="$target" \
      -o "$tmp_dir/new.s" >"$tmp_dir/new.log" 2>&1 || new_status=$?
    if ((old_status == 0 && new_status == 0)); then
      if cmp -s "$tmp_dir/old.s" "$tmp_dir/new.s"; then
        ((identical += 1))
      else
        printf 'DIFF fixture=%s target=%s old=0 new=0\n' "$fixture" "$target"
        ((different += 1))
      fi
    elif ((old_status != 0 && new_status != 0)); then
      ((both_reject += 1))
    else
      printf 'DIFF fixture=%s target=%s old=%d new=%d\n' \
        "$fixture" "$target" "$old_status" "$new_status"
      ((different += 1))
    fi
  done
done
printf 'MINIC_INLINE_SPECIALIZATION_OWNER_DIFF identical=%d both_reject=%d differing=%d total=%d\n' \
  "$identical" "$both_reject" "$different" \
  "$(( ${#fixtures[@]} * ${#targets[@]} ))"
test "$different" -eq 0
