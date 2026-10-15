#!/usr/bin/env bash
# Run inside EXISTING gcc-single runner against already SHA-frozen Kbuild .i.
# NO fresh kernel download, no new workflow, and no Runtime promotion.
set -Eeuo pipefail
ev="${1:?cohort directory required}"
root="${2:?compiler bin directory required}"
rotated="${3:-false}"
integer_subphase="${4:-false}"
four_index="${5:-false}"
case "$four_index" in true|false) ;; *) exit 64;; esac
if [[ "$four_index" == true ]]; then
  work="$ev/boolean-hot/optimized-phase-four"
else
  work="$ev/boolean-hot/optimized-phase"
fi
mkdir -p "$work/before/bin" "$work/after/bin"
cp "$root/minic" "$work/before/bin/minic"
cp "$root/minic" "$work/after/bin/minic"
# The before AND after compiler use identical timing instrumentation.
python3 tools/ci/apply-perf-specialization-phase-trace-v0.py
optional_trace=()
if [[ "$integer_subphase" == true ]]; then
  python3 tools/ci/apply-perf-integer-loop-trace-v0.py
  optional_trace=(--expect-integer-loops)
fi
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain" \
  "$root/minic-cc" >/dev/null
cp "$root/minic-cc" "$work/before/bin/minic-cc"
# Only the two previously independently A/B-tested optimization families.
python3 tools/ci/apply-perf-symbolic-function-body-owner-filter-v0.py
python3 tools/ci/apply-perf-integer-call-expression-index-v1.py
if [[ "$four_index" == true ]]; then
  # The extra Parser hash and integer owner filter are already isolated.
  python3 tools/ci/apply-perf-parser-lookup-counters-v0.py
  python3 tools/ci/apply-perf-parser-function-hash-v0.py
  python3 tools/ci/apply-perf-integer-function-body-owner-filter-v1.py
fi
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain" \
  "$root/minic-cc" >/dev/null
cp "$root/minic-cc" "$work/after/bin/minic-cc"
test "$(sha256sum "$work/before/bin/minic-cc" | cut -d' ' -f1)" != \
     "$(sha256sum "$work/after/bin/minic-cc" | cut -d' ' -f1)"
git diff --check
python3 -m py_compile tools/ci/linux-perf-phase-trace-v1.py
python3 tools/ci/linux-perf-phase-trace-v1.py --self-test
if [[ "$rotated" == true ]]; then
  targets=(lib/crc32.o net/core/dev.o net/ipv4/tcp_input.o)
else
  targets=(kernel/bpf/btf.o kernel/bpf/verifier.o net/core/filter.o)
fi
for owner in "${targets[@]}"; do
  key="${owner//\//_}"
  frozen="$ev/boolean-hot/$key.frozen.i"
  test -s "$frozen"
  python3 tools/ci/linux-perf-phase-trace-v1.py \
    --input "$frozen" \
    --baseline "$work/before/bin/minic" \
    --candidate "$work/after/bin/minic" \
    --expect-specialization "${optional_trace[@]}" \
    --out "$work/results/$key" | tee "$work/$key-verdict.txt"
  grep -Fq 'MINIC_SPEC_PHASE_GROUP=PASS' "$work/$key-verdict.txt"
  if (("${#optional_trace[@]}")); then
    grep -Fq 'MINIC_INTEGER_SUBPHASE_GROUP=PASS mode=before' "$work/$key-verdict.txt"
    grep -Fq 'MINIC_INTEGER_SUBPHASE_GROUP=PASS mode=after' "$work/$key-verdict.txt"
  fi
done
echo "MINIC_OPTIMIZED_STAGE_PROFILE=PASS owners=3 four_index=$four_index attribution_only=true qemu_pending=true"
