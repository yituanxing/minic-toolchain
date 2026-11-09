#!/usr/bin/env bash
# Both RISC-V XLEN modes must preserve true secondary entry roots in typed
# MachineIR, including the unexported C labels in constant-specialized clones.
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
: "${MINIC_CC:=$root/build/bin/minic-cc}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cc -E -P -std=gnu11 \
  "$root/tests/compiler/c0/riscv_secondary_label_closure.c" \
  -o "$work/source.i"
for mode in rv32 rv64; do
  case "$mode" in
    rv32) target=riscv32-linux; arch=rv32i; abi=ilp32 ;;
    rv64) target=riscv64-linux; arch=rv64i; abi=lp64 ;;
  esac
  MINIC_RV_MACHINE_TRACE=1 "$MINIC_CC" \
    --target="$target" -march="$arch" -mabi="$abi" \
    -S "$work/source.i" -o "$work/$mode.s" \
    2>"$work/$mode.err"
  grep -Eq '^__minic_inline_spec_[0-9]+_[0-9]+:$' "$work/$mode.s"
  grep -Eq 'MACHINE_CORE_COVERAGE_EXTERNAL_ROOT function=__minic_inline_spec_[0-9]+_[0-9]+ core_block=[0-9]+ machine_block=[0-9]+ indirect=1 local_label=0 address_taken=0' \
    "$work/$mode.err"
  if grep -Eq 'MACHINE_CORE_COVERAGE_REJECT|RV[36][24]_TYPED_REJECT' \
      "$work/$mode.err"; then
    echo "RISCV_SECONDARY_LABEL=FAIL mode=$mode stage=typed_coverage" >&2
    exit 1
  fi
  echo "RISCV_SECONDARY_LABEL=PASS mode=$mode "\
       "specialized_clone=1 secondary_root=1 no_fake_entry_edge=1"
done
