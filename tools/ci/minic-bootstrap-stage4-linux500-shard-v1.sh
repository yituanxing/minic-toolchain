#!/usr/bin/env bash
# Stage4 compiler vs immutable Stage0 on the SAME SHA-pinned Linux 6.6.143
# first500 preprocessed .i files. Seven existing CI shards; GNU AS only as
# an independent object-reference oracle, NOT proof of all-Mini Linux linking.
set -Eeuo pipefail
cd "$(dirname "$0")/../.."
label="${1:?shard label required}"
case "$label" in
 a) start=0;   count=72 ;;
 b) start=72;  count=72 ;;
 c) start=144; count=72 ;;
 d) start=216; count=71 ;;
 e) start=287; count=71 ;;
 f) start=358; count=71 ;;
 g) start=429; count=71 ;;
 *) echo "BOOTSTRAP_STAGE4_LINUX500=FAIL unknown label $label" >&2; exit 2 ;;
esac
root="$PWD/build/bootstrap-stage4-linux500"
work="$root/$label"
mkdir -p "$work"
seed_commit=3b6c1e335342b042f5f3c0b922f31843e6824bc9
if ! git cat-file -e "$seed_commit^{commit}" 2>/dev/null; then
  git fetch --no-tags --depth=1 origin "$seed_commit"
fi
git diff --exit-code "$seed_commit" HEAD -- src/ include/ tools/minic-cc/ tests/compiler/bootstrap/b0-sources.txt
echo "BOOTSTRAP_STAGE4_LINUX500_SOURCE=PASS unchanged_compiler_production=true label=$label"
sudo apt-get -o Acquire::Retries=3 install -y --no-install-recommends \
  gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu libc6-dev-riscv64-cross \
  qemu-user make python3 >/dev/null
stage4="$root/artifact/full-native/stage4/bin/minic-cc"
stage3="$root/artifact/full-native/stage3/summary.json"
stage4sum="$root/artifact/full-native/stage4/summary.json"
test -s "$stage4" && test -s "$stage3" && test -s "$stage4sum"
expected=50b3f7763bde2505e17bb545513cb00664286d4e762b3395aea3f29ce724ef31
printf '%s  %s\n' "$expected" "$stage4" | sha256sum -c -
python3 - "$stage3" "$stage4sum" "$expected" <<'PY'
import json, sys
stage3, stage4 = (json.load(open(x)) for x in sys.argv[1:3])
sha = sys.argv[3]
tools = {"minic-cc","minic-cpp","minic-as","minic-ar","minic-ld",
         "minic-objcopy","minic-nm","minic-strip","minic"}
assert stage3["round"]=="stage3" and stage4["round"]=="stage4"
assert stage3["passed"]==stage4["passed"]==77
assert not stage3["failed"] and not stage4["failed"]
assert set(stage4["linked"])==tools and stage3["linked"]==stage4["linked"]
assert stage4["linked"]["minic-cc"]==sha
a={row["source"]:row for row in stage3["rows"]}
for row in stage4["rows"]:
    prev=a[row["source"]]
    assert all(row[key]==prev[key] for key in ("i","s","o"))
print("BOOTSTRAP_STAGE4_LINUX_CERT=PASS stage3_stage4=77/77 tools=9/9 sha="+sha,flush=True)
PY
chmod 755 "$stage4"
wrapper="$root/minic-stage4"
printf '#!/bin/sh\nexec qemu-riscv64 "%s" "$@"\n' "$stage4" > "$wrapper"
chmod 755 "$wrapper"
"$wrapper" --help > "$work/stage4-help.log"
stage0="$root/stage0/bin/minic-cc"
MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1 \
  make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$root/stage0" "$stage0" >"$work/stage0-build.log" 2>&1 || {
  tail -n 100 "$work/stage0-build.log" >&2; exit 1;
}
printf '%s  %s\n' f04794d3132631059c028e0a889bf8cf06f15e8f24afcc70d24697edce4925fd "$stage0" | sha256sum -c -
# Frozen Linux kernel/configs.o needs a separate .incbin sidecar not present
# in its .i. Historical B4 comparison used an empty stub for PARITY ONLY.
if [[ "$label" == d ]]; then
  test ! -e kernel/config_data.gz
  mkdir -p kernel
  : > kernel/config_data.gz
  trap 'rm -f kernel/config_data.gz; rmdir kernel 2>/dev/null || true' EXIT
  echo "BOOTSTRAP_STAGE4_LINUX_AUX=kernel/config_data.gz empty_standin=true production_image=false" | tee "$work/auxiliary.txt"
fi
python3 -m py_compile tools/ci/minic-bootstrap-stage2-linux-sample-v1.py
python3 tools/ci/minic-bootstrap-stage2-linux-sample-v1.py \
  --corpus "$PWD/build/linux-perf-corpus" --stage0 "$stage0" \
  --candidate "$wrapper" --candidate-label stage4 \
  --out "$work/differential" --range-offset "$start" --range-count "$count" --jobs 4 \
  | tee "$work/verdict.log"
grep -Fq "BOOTSTRAP_STAGE4_LINUXRANGE=PASS owners=$count/$count" "$work/verdict.log"
echo "BOOTSTRAP_STAGE4_LINUX500_SHARD=PASS label=$label offset=$start count=$count stage4_sha256=$expected full_kernel_P1=NOT_RUN"
