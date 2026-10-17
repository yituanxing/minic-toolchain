#!/usr/bin/env bash
# Install SHA-certified native Stage4 compiler as the existing Kbuild
# host driver's ONLY compiler.  Retain host Stage0 for provenance and
# use the exact already-proven Stage2 wrapper strategy; no GNU CC fallback.
# Usage: bash script <existing-host-toolchain-bin> <stage4-elf>
set -Eeuo pipefail
[[ $# -eq 2 ]] || { echo "BOOTSTRAP_STAGE4_INSTALL=FAIL arguments" >&2; exit 64; }
bin=$(realpath "$1")
guest=$(realpath "$2")
sha4=50b3f7763bde2505e17bb545513cb00664286d4e762b3395aea3f29ce724ef31
sha0=f04794d3132631059c028e0a889bf8cf06f15e8f24afcc70d24697edce4925fd
test -x "$bin/minic" && test -x "$bin/minic-cc" && test -s "$guest"
printf '%s  %s\n' "$sha4" "$guest" | sha256sum -c -
printf '%s  %s\n' "$sha0" "$bin/minic-cc" | sha256sum -c -
command -v qemu-riscv64 >/dev/null
for file in minic.stage0 minic-cc.stage0 minic-cc.stage4.rv64; do
  test ! -e "$bin/$file"
done
mv "$bin/minic" "$bin/minic.stage0"
mv "$bin/minic-cc" "$bin/minic-cc.stage0"
cp "$guest" "$bin/minic-cc.stage4.rv64"
chmod 755 "$bin/minic-cc.stage4.rv64"
printf '%s  %s\n' "$sha4" "$bin/minic-cc.stage4.rv64" | sha256sum -c -
cat >"$bin/minic-cc" <<'SH'
#!/bin/sh
set -eu
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -n "${COHORT_STAGE2_INVOKE_TRACE:-}" ]; then
  input= output= prior=
  for arg do
    case "$prior" in
      -S) input=$arg ;;
      -o) output=$arg ;;
    esac
    prior=$arg
  done
  printf '%s\t%s\n' "$input" "$output" >> "$COHORT_STAGE2_INVOKE_TRACE"
fi
exec qemu-riscv64 "$dir/minic-cc.stage4.rv64" "$@"
SH
cat >"$bin/minic" <<'SH'
#!/usr/bin/env bash
set -Eeuo pipefail
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# The host driver uses argv[0] to find a sibling compiler shim.
exec -a "$dir/minic" "$dir/minic.stage0" "$@"
SH
chmod 755 "$bin/minic" "$bin/minic-cc"
work=$(mktemp -d "$PWD/build/minic-stage4-driver-smoke.XXXXXXXX")
trap 'rm -rf "$work"' EXIT
cp tests/compiler/c0/return_42.c "$work/in.c"
riscv64-linux-gnu-gcc -E -P -x c "$work/in.c" -o "$work/in.i"
# This smoke is isolated. GNU preprocessing is the fixed Kbuild reference,
# but every -S invocation below MUST execute the Stage4 RV64 ELF under QEMU.
set +e
qemu-riscv64 "$bin/minic-cc.stage4.rv64" --help >"$work/help.out" 2>"$work/help.err"
rc_help=$?
MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1 \
  qemu-riscv64 "$bin/minic-cc.stage4.rv64" -S "$work/in.i" -o "$work/direct.s" \
  >"$work/direct.out" 2>"$work/direct.err"
rc_direct=$?
MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1 \
  "$bin/minic-cc" -S "$work/in.i" -o "$work/wrapped.s" \
  >"$work/wrapped.out" 2>"$work/wrapped.err"
rc_wrapper=$?
MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1 \
  "$bin/minic" -S "$work/in.i" -o "$work/driver.s" \
  >"$work/driver.out" 2>"$work/driver.err"
rc_driver=$?
set -e
echo "BOOTSTRAP_STAGE4_DRIVER_DIAG help=$rc_help direct=$rc_direct shim=$rc_wrapper driver=$rc_driver"
if [[ "$rc_help" != 0 || "$rc_direct" != 0 || "$rc_wrapper" != 0 || "$rc_driver" != 0 ]]; then
  for label in help direct wrapped driver; do
    test ! -s "$work/$label.err" || sed -n '1,70p' "$work/$label.err" >&2
  done
  echo "BOOTSTRAP_STAGE4_INSTALL=FAIL direct_or_driver_smoke" >&2
  exit 7
fi
cmp "$work/direct.s" "$work/wrapped.s"
cmp "$work/direct.s" "$work/driver.s"
test -s "$work/driver.s"
if [[ -n "${COHORT_STAGE2_INVOKE_TRACE:-}" ]]; then
  grep -Fq "$work/driver.s" "$COHORT_STAGE2_INVOKE_TRACE" || {
    echo "BOOTSTRAP_STAGE4_INSTALL=FAIL missing_guest_dispatch_trace" >&2
    exit 8
  }
fi
echo "BOOTSTRAP_STAGE4_INSTALL=PASS stage4_sha256=$sha4 no_host_compiler_fallback=true qemu_wrapper_driver_verified=true"
