#!/usr/bin/env bash
# Temporarily use SHA-attested Stage2 RV64 CC in the EXISTING seven-runner
# GNU-Kbuild driver. Original Stage0 is retained for independent verification.
# Usage: ... <existing-host-toolchain-bin> <downloaded-stage2>
set -Eeuo pipefail
[[ $# == 2 ]] || { echo "BOOTSTRAP_STAGE2_INSTALL=FAIL arguments" >&2; exit 64; }
bin=$(realpath "$1")
guest=$(realpath "$2")
test -x "$bin/minic" && test -x "$bin/minic-cc" && test -s "$guest"
sha2=17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d
sha0=f04794d3132631059c028e0a889bf8cf06f15e8f24afcc70d24697edce4925fd
printf '%s  %s\n' "$sha2" "$guest" | sha256sum -c -
printf '%s  %s\n' "$sha0" "$bin/minic-cc" | sha256sum -c -
test ! -e "$bin/minic.stage0"
test ! -e "$bin/minic-cc.stage0"
test ! -e "$bin/minic-cc.stage2.rv64"
command -v qemu-riscv64 >/dev/null
mv "$bin/minic" "$bin/minic.stage0"
mv "$bin/minic-cc" "$bin/minic-cc.stage0"
cp "$guest" "$bin/minic-cc.stage2.rv64"
printf '%s  %s\n' "$sha2" "$bin/minic-cc.stage2.rv64" | sha256sum -c -
cat > "$bin/minic-cc" <<'SH'
#!/bin/sh
# Certified Stage2 RV64 SHA256:
# 17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d
set -eu
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -n "${COHORT_STAGE2_INVOKE_TRACE:-}" ]; then
  input=
  output=
  prior=
  for arg do
    case "$prior" in
      -S) input=$arg ;;
      -o) output=$arg ;;
    esac
    prior=$arg
  done
  printf '%s\t%s\n' "$input" "$output" >> "$COHORT_STAGE2_INVOKE_TRACE"
fi
exec qemu-riscv64 "$dir/minic-cc.stage2.rv64" "$@"
SH
# Cache identity hashes bin/minic, NOT only bin/minic-cc. An exact SHA-
# marked deterministic wrapper guarantees Stage2 & Stage0 cannot share a key.
cat > "$bin/minic" <<'SH'
#!/usr/bin/env bash
# Compiles with certified self-hosted Stage2 RV64 SHA256:
# 17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d
set -Eeuo pipefail
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# minic.stage0 selects sibling minic-cc from argv[0], not /proc/self/exe.
# Preserve argv[0] as $bin/minic while executing the *native* driver binary.
# Otherwise it tries nonexistent minic.stage0-cc or bypasses our Stage2 shim.
exec -a "$dir/minic" "$dir/minic.stage0" "$@"
SH
chmod 755 "$bin/minic" "$bin/minic-cc"
test -x "$bin/minic.stage0" && test -x "$bin/minic-cc.stage0"
test "$(sha256sum "$bin/minic-cc.stage2.rv64" | cut -d' ' -f1)" = "$sha2"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# Reuse the exact Stage2 B1 smoke source that already passed on the
# certified guest, rather than inventing an unvetted one-line probe.
cp tests/compiler/c0/return_42.c "$work/in.c"
riscv64-linux-gnu-gcc -E -P -x c "$work/in.c" -o "$work/in.i"
# The direct Stage2 output is the control arm for the native driver shim.
# If the driver fails, show independent wrapper/QEMU/driver exit status and
# argument dispatch before making any full Linux test attempt.
set +e
MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1 \
  qemu-riscv64 "$bin/minic-cc.stage2.rv64" -S "$work/in.i" -o "$work/direct.s" \
    >"$work/direct.stdout" 2>"$work/direct.stderr"
direct_rc=$?
MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1 \
  "$bin/minic-cc" -S "$work/in.i" -o "$work/wrapped.s" \
    >"$work/wrapped.stdout" 2>"$work/wrapped.stderr"
wrapped_rc=$?
MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1 \
  "$bin/minic" -S "$work/in.i" -o "$work/out.s" \
    >"$work/driver.stdout" 2>"$work/driver.stderr"
driver_rc=$?
set -e
echo "BOOTSTRAP_STAGE2_DRIVER_DIAG direct_rc=$direct_rc wrapper_rc=$wrapped_rc driver_rc=$driver_rc direct_s=$(test -s "$work/direct.s" && echo yes || echo no) wrapper_s=$(test -s "$work/wrapped.s" && echo yes || echo no) driver_s=$(test -s "$work/out.s" && echo yes || echo no)"
if [[ "$direct_rc" != 0 || "$wrapped_rc" != 0 || "$driver_rc" != 0 ]]; then
  for mode in direct wrapped driver; do
    echo "BOOTSTRAP_STAGE2_DIAG_STDERR mode=$mode" >&2
    sed -n '1,80p' "$work/$mode.stderr" >&2
    echo "BOOTSTRAP_STAGE2_DIAG_STDOUT mode=$mode" >&2
    sed -n '1,50p' "$work/$mode.stdout" >&2
  done
  if [[ -n "${COHORT_STAGE2_INVOKE_TRACE:-}" ]]; then
    echo "BOOTSTRAP_STAGE2_DIAG_TRACE_BEGIN" >&2
    tail -n 20 "$COHORT_STAGE2_INVOKE_TRACE" >&2
  fi
  echo "BOOTSTRAP_STAGE2_INSTALL=FAIL driver_to_guest_stage2_smoke" >&2
  exit 7
fi
cmp "$work/direct.s" "$work/wrapped.s"
cmp "$work/direct.s" "$work/out.s"
test -s "$work/out.s"
# Prove the driver really dispatched to Stage2, not the saved host binary.
if [[ -n "${COHORT_STAGE2_INVOKE_TRACE:-}" ]]; then
  grep -Fq "$work/out.s" "$COHORT_STAGE2_INVOKE_TRACE" || {
    echo "BOOTSTRAP_STAGE2_INSTALL=FAIL missing_stage2_smoke_trace" >&2
    exit 7
  }
fi
echo "BOOTSTRAP_STAGE2_INSTALL=PASS stage0_cc=$sha0 stage2_guest=$sha2 driver_sha=$(sha256sum "$bin/minic" | cut -d' ' -f1) no_host_compiler_fallback=true smoke_driver_to_qemu=true"
