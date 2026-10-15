#!/usr/bin/env bash
# A/B a single new algorithm after the verified symbolic+integer-CALL baseline.
# Only compare exact SHA-frozen GNU-Kbuild inputs, fail closed on asm changes.
set -Eeuo pipefail
ev="${1:?cohort path}"
root="${2:?MiniC bin path}"
rotation="${3:-false}"
case "$rotation" in true|false) ;; *) echo 'INTEGER_OWNER=FAIL bad rotation flag' >&2; exit 2;; esac
work="$ev/boolean-hot/integer-owner"
mkdir -p "$work/baseline/bin" "$work/filtered/bin"
cp "$root/minic" "$work/baseline/bin/minic"
cp "$root/minic" "$work/filtered/bin/minic"
python3 tools/ci/apply-perf-symbolic-function-body-owner-filter-v0.py
python3 tools/ci/apply-perf-integer-call-expression-index-v1.py
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain" \
  "$root/minic-cc" >/dev/null
cp "$root/minic-cc" "$work/baseline/bin/minic-cc"
python3 tools/ci/apply-perf-integer-function-body-owner-filter-v1.py
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain" \
  "$root/minic-cc" >/dev/null
cp "$root/minic-cc" "$work/filtered/bin/minic-cc"
test "$(sha256sum "$work/baseline/bin/minic-cc" | cut -d' ' -f1)" != \
     "$(sha256sum "$work/filtered/bin/minic-cc" | cut -d' ' -f1)"
git diff --check
if [[ "$rotation" == true ]]; then
  owners=(lib/crc32.o net/core/dev.o net/ipv4/tcp_input.o)
else
  owners=(kernel/bpf/btf.o kernel/bpf/verifier.o net/core/filter.o)
fi
for owner in "${owners[@]}"; do
  key="${owner//\//_}"
  test -s "$ev/boolean-hot/$key.frozen.i"
  python3 tools/ci/linux-perf-pinned-single-paired-ab-v1.py \
    --input "$ev/boolean-hot/$key.frozen.i" \
    --baseline-driver "$work/baseline/bin/minic" \
    --candidate-driver "$work/filtered/bin/minic" \
    --repeats 2 --out "$work/results/$key" | tee "$work/$key-screen.txt"
  grep -Fq 'MINIC_ONE_OWNER_AB=PASS' "$work/$key-screen.txt"
  echo "INTEGER_OWNER=PASS owner=$owner asm_equal=true"
done
echo 'INTEGER_OWNER_GROUP=PASS owners=3 only_integer_owner_filter_changed=true QEMU_pending=true'
