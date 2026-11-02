#!/usr/bin/env bash
# Optional exact-input differential gate for CALL/INDIRECT_CALL/PARAMETER and
# record return/varargs. Caller supplies a prior compiler, not a new oracle.
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
  core_fixed_call_scalar_conversions
  record_call_argument_materialization
  lp64_empty_record_call_abi
  aarch64_indirect_record_return_minic
  gnu_efi_record_msabi_prefix_callback
  rv64_lp64_softfloat_record_call
  variadic_direct_call
  variadic_indirect_call
  variadic_callee_save
  function_designator_call
  gnu_function_pointer_bridge_call
  gnu_pointer_sign_call_conversion
  gnu_indirect_fp_record_abi_negative
  gnu_variadic_single_scalar_record
  gnu_variadic_multifield_record_negative
  gnu_const_record_compound_call
  m150_indirect_variadic_call
  m151_indirect_call_batch
  x86_variadic_no_sse
  rv64_hardfloat_record_abi_probe
  rv32_variadic_record_abi_alignment
  loongarch64_syscall_alias_policy
  aarch64_weak_direct_call_got
  gnu_function_pointer_to_void_call
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
printf 'MINIC_FIVE_BACKEND_CORE_ABI_DIFF identical=%d both_reject=%d differing=%d total=%d\n' \
  "$identical" "$both_reject" "$different" "$(( ${#fixtures[@]} * ${#targets[@]} ))"
test "$different" -eq 0
