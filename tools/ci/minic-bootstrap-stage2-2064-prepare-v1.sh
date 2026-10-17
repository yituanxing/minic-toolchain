#!/usr/bin/env bash
# Source inside existing seven-shard GNU Kbuild pipeline after compiler build.
# Usage: source ./tools/ci/minic-bootstrap-stage2-2064-prepare-v1.sh producer|receiver "<evidence>"
set -Eeuo pipefail
stage2_role="$1"
stage2_ev="$2"
case "$stage2_role" in producer|receiver) ;; *) echo "BOOTSTRAP_STAGE2_2064_PREPARE=FAIL role" >&2; exit 7 ;; esac
test -d "$stage2_ev"
seed=3b6c1e335342b042f5f3c0b922f31843e6824bc9
if ! git cat-file -e "$seed^{commit}" 2>/dev/null; then
  git fetch --quiet --no-tags --depth=1 origin "$seed"
fi
git diff --exit-code "$seed" "$GITHUB_SHA" -- src/ include/ tools/minic-cc/ tests/compiler/bootstrap/b0-sources.txt
sudo apt-get -o Acquire::Retries=3 install -y --no-install-recommends qemu-user >/dev/null
guest="$GITHUB_WORKSPACE/build/bootstrap-stage2-linux2064/artifact/build/bootstrap-current-b1/stage2/minic-stage2-rv64"
bin="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain/bin"
test -s "$guest" && test -x "$bin/minic"
bash tools/ci/minic-bootstrap-stage2-install-kbuild-v1.sh "$bin" "$guest"
export COHORT_GNU_PREFLIGHT_ALLOW_SEVEN_SHARD_VOLATILE=1
export COHORT_GNU_PREFLIGHT_ALLOW_SEVEN_RECEIVER_VOLATILE=1
export COHORT_PROFILE_STAGES=0
if [[ "$stage2_role" == producer ]]; then
  export COHORT_STAGE2_INVOKE_TRACE="$stage2_ev/stage2-invocations.tsv"
  : > "$COHORT_STAGE2_INVOKE_TRACE"
fi
echo "BOOTSTRAP_STAGE2_2064_PREPARE=PASS role=$stage2_role certified_stage2=true"
