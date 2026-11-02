#!/usr/bin/env bash
# Optional same-preprocessed-source differential gate for the P6 Core
# execution-type closure. Emitters must retain machine bytes and rejection
# behavior while their C descriptor fallbacks are retired together.
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
baseline=${MINIC_BASELINE_CC:?set MINIC_BASELINE_CC to the prior minic-cc}
current=${MINIC_CURRENT_CC:-"$repo_root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
test -x "$baseline"
test -x "$current"
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT

fixtures=(
  gnu_overflow_builtins
  gnu_builtin_overflow_predicate
  core_integer_add_overflow
  core_integer_subtract_overflow
  core_integer_multiply_overflow_runtime
  riscv_packed_overflow_alignment
  rv32_u64_compound_or_store
  rv32_wide_branch_condition
  rv32_packed_scalar_alignment
  rv32_ffsll_high_word
  dynamic_stack_alloca
  pointer_integer_casts
  aarch64_linux_pointer_narrow_bitcast
  core_scalar_lvalue_bitcast
  cast_integer_conversion
  packed_record_layout
  rv64_record_load_packed
  gnu_typeof_array_bound_cast
  core_integer_or_shifts_m22
  gnu_choose_dynamic_condition_reject
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
printf 'MINIC_FIVE_BACKEND_CORE_EXEC_TYPE_DIFF identical=%d both_reject=%d differing=%d total=%d\n' \
  "$identical" "$both_reject" "$different" \
  "$(( ${#fixtures[@]} * ${#targets[@]} ))"
test "$different" -eq 0
