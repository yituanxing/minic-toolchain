#!/usr/bin/env bash
# Existing seven-runner CI uses frozen first500 Linux Kbuild .i inputs.
# Certified Stage2 in QEMU vs exact Stage0; same GNU assembler, .s/.o SHA.
set -Eeuo pipefail
cd "$(dirname "$0")/../.."
label="$1"
case "$label" in
 a) start=0; count=72 ;;
 b) start=72; count=72 ;;
 c) start=144; count=72 ;;
 d) start=216; count=71 ;;
 e) start=287; count=71 ;;
 f) start=358; count=71 ;;
 g) start=429; count=71 ;;
 *) echo "BOOTSTRAP_STAGE2_LINUX500=FAIL unknown label $label" >&2; exit 2 ;;
esac
root="$PWD/build/bootstrap-stage2-linux500"
work="$root/$label"
mkdir -p "$work"
seed_commit=3b6c1e335342b042f5f3c0b922f31843e6824bc9
if ! git cat-file -e "$seed_commit^{commit}" 2>/dev/null; then
 git fetch --no-tags --depth=1 origin "$seed_commit"
fi
git diff --exit-code "$seed_commit" HEAD -- src/ include/ tools/minic-cc/ tests/compiler/bootstrap/b0-sources.txt
echo "BOOTSTRAP_STAGE2_LINUX500_SOURCE=PASS label=$label"
sudo apt-get -o Acquire::Retries=3 install -y --no-install-recommends gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu libc6-dev-riscv64-cross qemu-user make python3 >/dev/null
stage2="$root/artifact/build/bootstrap-current-b1/stage2/minic-stage2-rv64"
test -s "$stage2"
printf '%s  %s\n' 17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d "$stage2" | sha256sum -c -
chmod +x "$stage2"
wrapper="$root/stage2-wrapper.sh"
cat >"$wrapper" <<EOF
#!/bin/sh
exec qemu-riscv64 "$stage2" "\$@"
EOF
chmod +x "$wrapper"
stage0="$root/stage0/bin/minic-cc"
MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1 \
 make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$root/stage0" "$stage0" >"$work/stage0-build.log" 2>&1 || {
 tail -n 100 "$work/stage0-build.log" >&2; exit 1;
}
printf '%s  %s\n' f04794d3132631059c028e0a889bf8cf06f15e8f24afcc70d24697edce4925fd "$stage0" | sha256sum -c -
# kernel/configs.o has a GNU .incbin auxiliary file *outside* its frozen
# .i; historic certified B4 replay likewise injected an EMPTY stub only to
# test assembler parity. Never claim this object is a bootable production .o.
if [[ "$label" == d ]]; then
  test ! -e kernel/config_data.gz
  mkdir -p kernel
  : > kernel/config_data.gz
  trap 'rm -f kernel/config_data.gz; rmdir kernel 2>/dev/null || true' EXIT
  echo "BOOTSTRAP_STAGE2_LINUX500_AUX=kernel/config_data.gz empty_standin=true production_link_oracle=false" | tee "$work/auxiliary.txt"
fi
python3 -m py_compile tools/ci/minic-bootstrap-stage2-linux-sample-v1.py
python3 tools/ci/minic-bootstrap-stage2-linux-sample-v1.py \
 --corpus "$PWD/build/linux-perf-corpus" --stage0 "$stage0" --stage2 "$wrapper" \
 --out "$work/differential" --range-offset "$start" --range-count "$count" --jobs 4 | tee "$work/verdict.log"
grep -Fq "BOOTSTRAP_STAGE2_LINUXRANGE=PASS owners=$count/$count" "$work/verdict.log"
echo "BOOTSTRAP_STAGE2_LINUX500_SHARD=PASS label=$label offset=$start count=$count"
