#!/usr/bin/env bash
# Apply ONLY the three jointly first500-certified optimizations after the
# existing, unmodified Linux Runtime/Crossblock profile was built.
# Every seven-way producer and the receiver must run this identical wrapper.
# Canonical src/target now contains promoted explicit ELF flags and the
# .L*-only PI addressing selector. The existing Runtime static-PCREL patch
# strengthens that selector to all-lla before these four performance patches.
# This opt-in wrapper is still required until the remaining performance source
# overlays themselves are promoted into tracked native C.
set -Eeuo pipefail
if [[ $# -ne 2 ]]; then
  echo "usage: $0 <existing-runtime-build-dir> <evidence-dir>" >&2
  exit 64
fi
build_dir="$1"
ev="$2"
test -x "$build_dir/bin/minic"
test -x "$build_dir/bin/minic-cc"
mkdir -p "$ev"
if [[ -f docs/ci/native-runtime44-perf4-validated-sha1.txt ]]; then
  # The exact four once-applied performance changes are part of canonical C.
  # Runtime profile checks all seven SHA1s before the toolchain is built.
  test -x "$build_dir/bin/minic"
  test -x "$build_dir/bin/minic-cc"
  echo "MINIC_PERF_INTEGRATED_FULL=ALREADY_NATIVE source=validated_runtime44_perf4" |
    tee "$ev/perf-integrated-profile.txt"
  exit 0
fi
before=$(sha256sum "$build_dir/bin/minic-cc" | cut -d' ' -f1)
# Their order is part of the existing 500/500 real-Kbuild certification.
python3 tools/ci/apply-perf-symbolic-function-body-owner-filter-v0.py
python3 tools/ci/apply-perf-integer-call-expression-index-v1.py
python3 tools/ci/apply-perf-parser-lookup-counters-v0.py
python3 tools/ci/apply-perf-parser-function-hash-v0.py
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$build_dir" \
  "$build_dir/bin/minic" "$build_dir/bin/minic-cc" >/dev/null
after=$(sha256sum "$build_dir/bin/minic-cc" | cut -d' ' -f1)
test "$before" != "$after"
printf 'MINIC_PERF_INTEGRATED_FULL=READY original_cc=%s new_cc=%s first500=500/500 qemu_pending=true\n' \
  "$before" "$after" | tee "$ev/perf-integrated-profile.txt"
