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
# GNU host GCC defaults to PIE on x86_64. Replacing its objects with non-PIC
# MiniC output makes the original BusyBox link reject PC32 relocations; use
# MiniC's qualified x86 GOT/PLT -fPIE backend instead.
if timeout 900s env CROSS_COMPILE="$prefix" \
    PKG_CONFIG=pkg-config \
    BUSYBOX_ALLYES_LINK_COMPATIBLE=1 \
    BUSYBOX_ALLYES_MIXED_BULK=1 \
    BUSYBOX_ALLYES_CROSS_PREFIX="$prefix" \
    BUSYBOX_ALLYES_MINIC_TARGET="$target-linux" \
    BUSYBOX_ALLYES_MINIC_PIC="$(case "$target" in riscv64|x86_64) printf 1;; *) printf 0;; esac)" \
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
grep -E '^(BUSYBOX_ALLYES_(CONFIG=|HOST_EXCLUSIONS=|GCC_KBUILD=|GCC_BUILD_DIAGNOSTIC=|GCC_FIRST_ERRORS=|GCC_SEQUENTIAL=|GCC_REAL_ERRORS |GCC_STOP_CONTEXT |GCC_FINAL_LINK_LOG=|GCC_FINAL_LINK_CAUSE |GCC_FINAL_LINK_FIRST_LINES=|GCC_FINAL_LINK_LAST_LINES=|GCC_LINK_FEATURES=|GCC_LINK_ATTEMPTS=|FRONTIER=|SCAN=|GCC_RUNTIME_ORACLE=)|BUSYBOX_MIXED_BULK(_CROSS_RUNTIME=|_CROSS_COHORT=|_SOURCE=|_REAL_OWNERS=|_RUNTIME=|_RUNTIME_DIFF|=))' \
  "$work/probe.out" || true
grep -E '^BUSYBOX_ALLYES_BLOCKER ' "$work/probe.out" | head -n 30 || true
grep -E '^BUSYBOX_ALLYES_CORE_REJECT ' "$work/probe.out" | head -n 65 || true
grep -E '^BUSYBOX_ALLYES_CPP_CONTEXT ' "$work/probe.out" | head -n 16 || true
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
  echo "FOUR_BACKEND_BUSYBOX_ALL_KBUILD=FAIL target=$target stage=no_usable_GNU_Kbuild_census scan_exit=$scan_rc" >&2
  tail -n 25 "$work/probe.out" "$work/probe.err" >&2 || true
  exit 1
fi
# Promote the existing full ORIGINAL allyesconfig/Kbuild oracle. Previous
# exploration returned zero even when mixed replacement failed; this contract
# now requires every SHA receipt, original GNU full link, a changed ELF and
# all eleven deterministic byte-exact GNU/QEMU applet comparisons.
if [ "$scan_rc" -ne 0 ]; then
  echo "FOUR_BACKEND_BUSYBOX_ALL_ACCEPT=FAIL target=$target stage=full_Kbuild_probe status=$scan_rc" >&2
  exit 1
fi
case "$target" in
  riscv64) floor=500 ;;
  aarch64) floor=581 ;;
  loongarch64) floor=581 ;;
  *) echo "unsupported cross target: $target" >&2; exit 1 ;;
esac
grep -qx 'BUSYBOX_ALLYES_GCC_KBUILD=PASS original_Kconfig=1 original_sources=1' "$work/probe.out" ||
  { echo "FOUR_BACKEND_BUSYBOX_ALL_ACCEPT=FAIL target=$target stage=GNU_Kbuild" >&2; exit 1; }
grep -qx 'BUSYBOX_ALLYES_GCC_RUNTIME_ORACLE=PASS applets=6 original_full_Kbuild=1' "$work/probe.out" ||
  { echo "FOUR_BACKEND_BUSYBOX_ALL_ACCEPT=FAIL target=$target stage=GNU_QEMU_oracle" >&2; exit 1; }
grep -qx 'BUSYBOX_MIXED_BULK_RUNTIME=DISCOVERY fixtures=38 matches=38 differences=0' "$work/probe.out" ||
  { echo "FOUR_BACKEND_BUSYBOX_ALL_ACCEPT=FAIL target=$target stage=MiniC_GNU_QEMU_differential" >&2; exit 1; }
grep -qx 'BUSYBOX_MIXED_BULK_CROSS_RUNTIME=PASS cases=38 GNU_QEMU_byte_exact=1' "$work/probe.out" ||
  { echo "FOUR_BACKEND_BUSYBOX_ALL_ACCEPT=FAIL target=$target stage=cross_applet_coverage" >&2; exit 1; }
receipt=$(grep '^BUSYBOX_MIXED_BULK=PASS MiniC_owners=' "$work/probe.out" || true)
owner_receipt=$(grep '^BUSYBOX_MIXED_BULK_REAL_OWNERS=PASS total=' "$work/probe.out" || true)
coverage=$(grep '^BUSYBOX_MIXED_BULK_CROSS_COHORT=DISCOVERY original_Kbuild_owners=' "$work/probe.out" || true)
owner_manifest=$(grep '^BUSYBOX_MIXED_BULK_ALL_C_OWNERS=PASS original_Kbuild_owners=' "$work/probe.out" || true)
mini=$(printf '%s\n' "$receipt" | sed -n 's/.*MiniC_owners=\([0-9][0-9]*\) GCC_fallback=.*/\1/p')
gnu=$(printf '%s\n' "$receipt" | sed -n 's/.* GCC_fallback=\([0-9][0-9]*\) original_full_Kbuild=.*/\1/p')
case "$receipt" in
  *" original_full_Kbuild=1 eleven_fixture_byte_exact=1 MiniC_and_GNU_binary_differ=1 full_all_MiniC=NO") ;;
  *) echo "FOUR_BACKEND_BUSYBOX_ALL_ACCEPT=FAIL target=$target stage=missing_real_mixed_receipt" >&2; exit 1 ;;
esac
if [ -z "$mini" ] || [ -z "$gnu" ] || [ "$mini" -lt "$floor" ] ||
   [ "$gnu" -ne 0 ] ||
   { [ "$target" != riscv64 ] && [ "$mini" -ne 581 ]; } ||
   ! printf '%s\n' "$owner_manifest" |
       grep -qx "BUSYBOX_MIXED_BULK_ALL_C_OWNERS=PASS original_Kbuild_owners=$mini MiniC_owners=$mini GNU_C_fallbacks=0 SHA256_verified=$mini link_toolchain=GNU" ||
   ! printf '%s\n' "$owner_receipt" |
       grep -qx "BUSYBOX_MIXED_BULK_REAL_OWNERS=PASS total=$mini SHA256_verified=$mini original_Kbuild=1" ||
   ! printf '%s\n' "$coverage" |
       grep -qx "BUSYBOX_MIXED_BULK_CROSS_COHORT=DISCOVERY original_Kbuild_owners=$mini MiniC_qualified=$mini GNU_fallbacks=0 full_MiniC=NO"; then
  echo "FOUR_BACKEND_BUSYBOX_ALL_ACCEPT=FAIL target=$target stage=coverage_or_SHA" >&2
  exit 1
fi
echo "FOUR_BACKEND_BUSYBOX_ALL_ACCEPT=PASS target=$target original_Kbuild_owners=$mini MiniC_owners=$mini GNU_fallbacks=$gnu SHA256_verified=$mini GNU_QEMU_fixtures=38 strict=1 all_C_owners_MiniC=YES assembler_linker=GNU full_all_MiniC=NO"
echo "FOUR_BACKEND_BUSYBOX_ALL_KBUILD_END target=$target authenticated_mixed_build=1 full_MiniC=NO"
