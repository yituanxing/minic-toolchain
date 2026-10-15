#!/usr/bin/env bash
# Reuse exact runtime workflow owner + SHA-frozen Kbuild inputs.
set -Eeuo pipefail
ev="${1:?cohort directory}"
root="${2:?MiniC runtime bin directory}"
rotated="${3:-false}"
optimized="${4:-false}"
symbolic_subphase="${5:-false}"
integer_subphase="${6:-false}"
if [[ "$optimized" == true ]]; then
  exec bash tools/ci/linux-perf-diagnose-optimized-phase-v1.sh \
    "$ev" "$root" "$rotated" "$integer_subphase"
fi
work="$ev/boolean-hot/spec-phase"
mkdir -p "$work/baseline/bin" "$work/traced/bin"
cp "$root/minic" "$work/baseline/bin/minic"
cp "$root/minic" "$work/traced/bin/minic"
cp "$root/minic-cc" "$work/baseline/bin/minic-cc"
python3 tools/ci/apply-perf-specialization-phase-trace-v0.py
optional_trace=()
if [[ "$symbolic_subphase" == true ]]; then
  python3 tools/ci/apply-perf-symbolic-loop-trace-v0.py
  optional_trace=(--expect-symbolic-loops)
fi
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain" \
  "$root/minic-cc" >/dev/null
cp "$root/minic-cc" "$work/traced/bin/minic-cc"
git diff --check
if [[ "$rotated" == true ]]; then
  targets=(net/core/dev.o net/ipv4/tcp_input.o lib/crc32.o)
else
  targets=(kernel/bpf/btf.o kernel/bpf/verifier.o net/core/filter.o)
fi
python3 tools/ci/linux-perf-phase-trace-v1.py --self-test
for owner in "${targets[@]}"; do
  key="${owner//\//_}"
  python3 tools/ci/linux-perf-phase-trace-v1.py \
    --input "$ev/boolean-hot/$key.frozen.i" \
    --baseline "$work/baseline/bin/minic" \
    --candidate "$work/traced/bin/minic" \
    --expect-specialization "${optional_trace[@]}" \
    --out "$work/results/$key" | tee "$work/$key-verdict.txt"
  grep -Fq 'MINIC_SPEC_PHASE_GROUP=PASS' "$work/$key-verdict.txt"
  if ((${#optional_trace[@]})); then
    grep -Fq 'MINIC_SYMBOLIC_SUBPHASE_GROUP=PASS' "$work/$key-verdict.txt"
  fi
done
echo 'MINIC_SPEC_GAP_GROUP=PASS owners=3 traced=true qemu_pending=true'
