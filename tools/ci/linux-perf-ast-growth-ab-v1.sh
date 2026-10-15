#!/usr/bin/env bash
# Isolated A/B, only difference the two large AST vector growth policies.
# Real, SHA-frozen Kbuild preprocessed inputs; abort on any assembly difference.
set -Eeuo pipefail
ev="${1:?evidence}"
root="${2:?bin}"
rotation="${3:-false}"
work="$ev/boolean-hot/ast-growth"
mkdir -p "$work/baseline/bin" "$work/growth/bin"
cp "$root/minic" "$work/baseline/bin/minic"
cp "$root/minic" "$work/growth/bin/minic"
cp "$root/minic-cc" "$work/baseline/bin/minic-cc"
python3 tools/ci/apply-perf-ast-capacity-growth-v1.py
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain" \
  "$root/minic-cc" >/dev/null
cp "$root/minic-cc" "$work/growth/bin/minic-cc"
test "$(sha256sum "$work/growth/bin/minic-cc" | cut -d' ' -f1)" != \
     "$(sha256sum "$work/baseline/bin/minic-cc" | cut -d' ' -f1)"
git diff --check
if [[ "$rotation" == true ]]; then
  owners=(lib/crc32.o net/core/dev.o net/ipv4/tcp_input.o)
else
  owners=(kernel/bpf/btf.o kernel/bpf/verifier.o net/core/filter.o)
fi
for owner in "${owners[@]}"; do
  key="${owner//\//_}"
  input="$ev/boolean-hot/$key.frozen.i"
  test -s "$input"
  echo "AST_GROWTH_SCREEN=START owner=$owner"
  python3 tools/ci/linux-perf-pinned-single-paired-ab-v1.py \
    --input "$input" \
    --baseline-driver "$work/baseline/bin/minic" \
    --candidate-driver "$work/growth/bin/minic" \
    --repeats 2 --out "$work/results/$key" | tee "$work/$key-verdict.txt"
  grep -Fq 'MINIC_ONE_OWNER_AB=PASS' "$work/$key-verdict.txt"
  for mode in baseline growth; do
    /usr/bin/time -v -o "$work/results/$key/$mode-rss.txt" \
      "$work/$mode/bin/minic" -S "$input" -o "$work/results/$key/$mode.s" \
      >"$work/results/$key/$mode.stdout" 2>"$work/results/$key/$mode.stderr"
  done
  cmp "$work/results/$key/baseline.s" "$work/results/$key/growth.s"
  echo "AST_GROWTH_SCREEN=PASS owner=$owner assembly_equal=true"
  for mode in baseline growth; do
    echo "AST_GROWTH_RSS owner=$owner mode=$mode $(grep 'Maximum resident set size' "$work/results/$key/$mode-rss.txt" | tr -s ' ')"
  done
done
echo 'AST_GROWTH_GROUP=PASS owners=3 byte_equivalent=true QEMU_pending=true'
