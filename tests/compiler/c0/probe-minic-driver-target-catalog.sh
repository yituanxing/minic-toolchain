#!/usr/bin/env bash
# Complete five-target product-driver -S contract vs the selected compiler.
# This intentionally NEVER claims non-RV64 MiniAS, MiniLD or sysroot support.
set -Eeuo pipefail
: "${MINIC:?provide staged minic path}"
: "${MINIC_CC:?provide minic-cc path}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/oracle.i" <<'EOF'
int target_data_model(void) {
    return (int)(sizeof(void *) * 100U + sizeof(long));
}
EOF
for arch in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
  extra=()
  case "$arch" in
    riscv32-linux) extra=(-march=rv32i -mabi=ilp32) ;;
    riscv64-linux) extra=(-march=rv64i -mabi=lp64) ;;
  esac
  "$MINIC_CC" "--target=$arch" "${extra[@]}" -S "$work/oracle.i" -o "$work/$arch-cc.s"
  "$MINIC" "--target=$arch" "${extra[@]}" -S "$work/oracle.i" -o "$work/$arch-driver.s"
  cmp "$work/$arch-cc.s" "$work/$arch-driver.s" || {
    echo "MINIC_DRIVER_TARGET_CATALOG=FAIL assembly_mismatch=$arch" >&2
    exit 1
  }
  grep -Fq 'target_data_model:' "$work/$arch-driver.s"
  if [[ "$arch" != riscv64-linux ]]; then
    if "$MINIC" "--target=$arch" -c "$work/oracle.i" \
         -o "$work/$arch-invalid.o" 2>"$work/$arch.err"; then
      echo "MINIC_DRIVER_TARGET_CATALOG=FAIL unqualified_stage=$arch" >&2
      exit 1
    fi
    grep -Fq 'target-requires-minic-cc-or-external-assembler:' "$work/$arch.err"
    test ! -e "$work/$arch-invalid.o"
  fi
done
if "$MINIC" --target=not-a-target -S "$work/oracle.i" \
    -o "$work/unknown.s" 2>"$work/unknown.err"; then
  echo "MINIC_DRIVER_TARGET_CATALOG=FAIL unknown_target" >&2
  exit 1
fi
grep -Fq 'unsupported-target:not-a-target' "$work/unknown.err"
test ! -e "$work/unknown.s"
if "$MINIC" --target=x86_64-linux -march=rv64i -S "$work/oracle.i" \
    -o "$work/bad-profile.s" 2>"$work/bad-profile.err"; then
  echo "MINIC_DRIVER_TARGET_CATALOG=FAIL bad_profile" >&2
  exit 1
fi
grep -Fq 'require RISC-V' "$work/bad-profile.err"
test ! -e "$work/bad-profile.s"
echo 'MINIC_DRIVER_TARGET_CATALOG=PASS five_assembly_targets=5 staged_link=RV64_only default_rv64_unchanged=1'
