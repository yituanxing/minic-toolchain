#!/bin/sh
# Full original BusyBox 1.36.1 Kconfig/Kbuild cross-ISA census.
# Unlike manually enumerated 179 selected owners, this asks upstream Kconfig
# for ALL enabled options and shadows *every actual GCC-owned C object*.
# Diagnostic until cross GNU baseline/relinked MiniC binary and QEMU pass.
set -eu
test "$#" -eq 6
: "${MINIC_CC:?set MINIC_CC}"
root=$1
target=$2
gcc=$3
prefix=$4
qemu=$5
sysroot=$6
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$work/src"
report="$work/report"
mkdir -p "$src" "$report"
(cd "$root" && tar -cf - .) | (cd "$src" && tar -xf -)
echo "FOUR_BACKEND_BUSYBOX_ALL_KBUILD_START target=$target original_commit=1a64f6a20aaf6ea4dbba68bbfa8cc1ab7e5c57c4 mode=upstream_allyesconfig"
# Reuse x86's host-link-compatible upstream Kconfig policy with the actual
# cross GNU compiler. Source and Kbuild itself remain pristine.
if timeout 900s env CROSS_COMPILE="$prefix" \
    PKG_CONFIG=pkg-config \
    BUSYBOX_ALLYES_LINK_COMPATIBLE=1 \
    BUSYBOX_ALLYES_MIXED_BULK=1 \
    BUSYBOX_ALLYES_CROSS_PREFIX="$prefix" \
    BUSYBOX_ALLYES_MINIC_TARGET="$target-linux" \
    BUSYBOX_ALLYES_QEMU="$qemu" \
    BUSYBOX_ALLYES_SYSROOT="$sysroot" \
    python3 "$repo/tests/external/busybox/allyes_sweep.py" \
      --source "$src" --report "$report" \
      --minic "$MINIC_CC" --host-cc "$gcc" \
      >"$work/probe.out" 2>"$work/probe.err"; then
    scan_rc=0
else
    scan_rc=$?
fi
grep -E '^(BUSYBOX_ALLYES_(CONFIG=|HOST_EXCLUSIONS=|GCC_KBUILD=|GCC_BUILD_DIAGNOSTIC=|GCC_FIRST_ERRORS=|GCC_SEQUENTIAL=|GCC_REAL_ERRORS |GCC_STOP_CONTEXT |GCC_FINAL_LINK_LOG=|FRONTIER=|SCAN=|GCC_RUNTIME_ORACLE=)|BUSYBOX_MIXED_BULK(_CROSS_COHORT=|_SOURCE=|_REAL_OWNERS=|_RUNTIME=|_RUNTIME_DIFF|=))' \
  "$work/probe.out" || true
grep -E '^BUSYBOX_ALLYES_BLOCKER ' "$work/probe.out" | head -n 30 || true
if [ -f "$report/summary.json" ]; then
  python3 - "$report/summary.json" "$target" "$scan_rc" <<'PY'
import json, sys
path, target, code = sys.argv[1:]
s = json.load(open(path, encoding="utf-8"))
total = s.get("attempted_C_TUs", 0)
mini = s.get("minic_object_pass", 0)
cpp = s.get("cpp_fail", 0)
front = s.get("minic_fail", 0)
asm = s.get("assembler_fail", 0)
gcc = s.get("gcc_fail", 0)
build = "PASS" if s.get("kbuild_completed") else "BLOCKED"
print("FOUR_BACKEND_BUSYBOX_ALL_KBUILD=DISCOVERY target=%s original_Kconfig=allyesconfig original_Kbuild=1 Kbuild_GNU_link=%s attempted_C_TUs=%d MiniC_GNU_AS_pass=%d CPP_blocked=%d MiniC_blocked=%d GNU_AS_blocked=%d original_GCC_failed=%d scan_exit=%s full_MiniC=NO" % (target, build, total, mini, cpp, front, asm, gcc, code), flush=True)
if total < 200:
    print("FOUR_BACKEND_BUSYBOX_ALL_KBUILD=INSUFFICIENT target=%s real_GNU_C_owners=%d min=200" % (target, total), flush=True)
PY
else
  echo "FOUR_BACKEND_BUSYBOX_ALL_KBUILD=BLOCKED target=$target stage=original_allyes_or_early_Kbuild scan_exit=$scan_rc"
  tail -n 25 "$work/probe.out" "$work/probe.err" || true
fi
echo "FOUR_BACKEND_BUSYBOX_ALL_KBUILD_END target=$target diagnostic_only=1 no_full_MiniC_claim=1"
