#!/usr/bin/env bash
# Structural-only phase memory audit. Same SHA-frozen Kbuild inputs in both
# arms; verify assembly identity and report exact AST footprint and peak RSS.
set -Eeuo pipefail
ev="${1:?cohort evidence path}"
root="${2:?MiniC runtime bin directory}"
rotation="${3:-false}"
work="$ev/boolean-hot/ast-footprint"
mkdir -p "$work/baseline/bin" "$work/traced/bin"
cp "$root/minic" "$work/baseline/bin/minic"
cp "$root/minic" "$work/traced/bin/minic"
cp "$root/minic-cc" "$work/baseline/bin/minic-cc"
python3 tools/ci/apply-perf-ast-footprint-trace-v1.py
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain" \
  "$root/minic-cc" >/dev/null
cp "$root/minic-cc" "$work/traced/bin/minic-cc"
git diff --check
if [[ "$rotation" == true ]]; then
  owners=(lib/crc32.o net/core/dev.o net/ipv4/tcp_input.o)
else
  owners=(kernel/bpf/btf.o kernel/bpf/verifier.o net/core/filter.o)
fi
for owner in "${owners[@]}"; do
  key="${owner//\//_}"
  frozen="$ev/boolean-hot/$key.frozen.i"
  test -s "$frozen"
  mkdir -p "$work/results/$key"
  "$work/baseline/bin/minic" -S "$frozen" -o "$work/results/$key/baseline.s" \
    >"$work/results/$key/baseline.stdout" 2>"$work/results/$key/baseline.stderr"
  /usr/bin/time -v -o "$work/results/$key/peak-rss.txt" \
    env MINIC_AST_FOOTPRINT=1 \
    "$work/traced/bin/minic" -S "$frozen" -o "$work/results/$key/traced.s" \
    >"$work/results/$key/traced.stdout" 2>"$work/results/$key/traced.stderr"
  cmp -s "$work/results/$key/baseline.s" "$work/results/$key/traced.s"
  grep -Fq 'MINIC_AST_FOOTPRINT ' "$work/results/$key/traced.stderr"
  sha256sum "$frozen" "$work/results/$key/traced.s"
  grep -F 'MINIC_AST_FOOTPRINT ' "$work/results/$key/traced.stderr"
  grep -E 'Maximum resident set size \(kbytes\)' "$work/results/$key/peak-rss.txt"
  echo "MINIC_AST_AUDIT=PASS owner=$owner assembly_equal=true"
done
echo 'MINIC_AST_AUDIT_GROUP=PASS owners=3 semantic_compiler_unchanged=true QEMU_pending=true'
