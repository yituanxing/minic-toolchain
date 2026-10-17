#!/usr/bin/env bash
# Source in existing 7-way Linux Kbuild pipeline after building host driver.
# Input Stage4 is immutable output of green GNU-free nine-tool fixedpoint
# CI #39058935623. The GNU-built host driver only routes to Stage4 RV64 CC.
set -Eeuo pipefail
stage4_role="${1:?role required}"
stage4_ev="${2:?evidence path required}"
case "$stage4_role" in producer|receiver) ;; *) echo "BOOTSTRAP_STAGE4_2064_PREPARE=FAIL role" >&2; exit 7 ;; esac
test -d "$stage4_ev"
seed=3b6c1e335342b042f5f3c0b922f31843e6824bc9
if ! git cat-file -e "$seed^{commit}" 2>/dev/null; then
  git fetch --quiet --no-tags --depth=1 origin "$seed"
fi
git diff --exit-code "$seed" "$GITHUB_SHA" -- src/ include/ tools/minic-cc/ tests/compiler/bootstrap/b0-sources.txt
sudo apt-get -o Acquire::Retries=3 install -y --no-install-recommends qemu-user >/dev/null
guest="$GITHUB_WORKSPACE/build/bootstrap-stage4-linux2064/artifact/full-native/stage4/bin/minic-cc"
bin="$GITHUB_WORKSPACE/build/linux-runtime-gcc-cohort-toolchain/bin"
sum="$GITHUB_WORKSPACE/build/bootstrap-stage4-linux2064/artifact/full-native/stage4/summary.json"
test -s "$guest" && test -s "$sum" && test -x "$bin/minic"
python3 - "$guest" "$sum" <<'PY'
import hashlib, json, sys
binary, manifest = sys.argv[1:3]
sha=hashlib.sha256(open(binary,"rb").read()).hexdigest()
expected="50b3f7763bde2505e17bb545513cb00664286d4e762b3395aea3f29ce724ef31"
report=json.load(open(manifest))
assert sha==expected==report["linked"]["minic-cc"]
assert report["round"]=="stage4" and report["passed"]==77 and not report["failed"]
assert len(report["linked"])==9
print("BOOTSTRAP_STAGE4_2064_CERT=PASS stage4_SHA="+sha+" original_77_of_77=true",flush=True)
PY
# The existing cohort wrapper calls this environment variable for
# compatibility with previously certified Stage2 Kbuild scripts; the evidence
# itself is explicitly Stage4 and is never mislabeled as a Stage2 binary.
export COHORT_STAGE2_INVOKE_TRACE="$stage4_ev/stage4-invocations.tsv"
: > "$COHORT_STAGE2_INVOKE_TRACE"
bash tools/ci/minic-bootstrap-stage4-install-kbuild-v1.sh "$bin" "$guest"
export COHORT_GNU_PREFLIGHT_ALLOW_SEVEN_SHARD_VOLATILE=1
export COHORT_GNU_PREFLIGHT_ALLOW_SEVEN_RECEIVER_VOLATILE=1
export COHORT_PROFILE_STAGES=0
echo "BOOTSTRAP_STAGE4_2064_PREPARE=PASS role=$stage4_role compiler_sha=50b3f7763bde2505e17bb545513cb00664286d4e762b3395aea3f29ce724ef31"
