#!/usr/bin/env bash
# Paired Parser hash A/B after prior tested symbolic and integer optimizations.
# Inputs, GNU and MiniC objects are exact-SHA frozen and reused by gcc-single.
set -Eeuo pipefail
ev="${1:?cohort path}"
root="${2:?compiler path}"
rotation="${3:-false}"
work="$ev/boolean-hot/parser-combined"
mkdir -p "$work/linear/bin" "$work/hashed/bin"
cp "$root/minic" "$work/linear/bin/minic"
cp "$root/minic" "$work/hashed/bin/minic"
# Both arms have identical SPECIALIZATION and lookup COUNTER code.
python3 tools/ci/apply-perf-symbolic-function-body-owner-filter-v0.py
python3 tools/ci/apply-perf-integer-call-expression-index-v1.py
python3 tools/ci/apply-perf-parser-lookup-counters-v0.py
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain" \
  "$root/minic-cc" >/dev/null
cp "$root/minic-cc" "$work/linear/bin/minic-cc"
# One change only: earliest-definition-preserving function lookup hash.
python3 tools/ci/apply-perf-parser-function-hash-v0.py
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain" \
  "$root/minic-cc" >/dev/null
cp "$root/minic-cc" "$work/hashed/bin/minic-cc"
test "$(sha256sum "$work/linear/bin/minic-cc" | cut -d' ' -f1)" != \
     "$(sha256sum "$work/hashed/bin/minic-cc" | cut -d' ' -f1)"
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
  python3 tools/ci/linux-perf-pinned-single-paired-ab-v1.py \
    --input "$input" \
    --baseline-driver "$work/linear/bin/minic" \
    --candidate-driver "$work/hashed/bin/minic" \
    --repeats 2 --out "$work/results/$key" | tee "$work/$key-screen.txt"
  grep -Fq 'MINIC_ONE_OWNER_AB=PASS' "$work/$key-screen.txt"
  echo "COMBINED_PARSER_HASH=PASS owner=$owner byte_identical=true"
done
echo 'COMBINED_PARSER_HASH_GROUP=PASS owners=3 only_function_lookup_changed=true QEMU_pending=true'
