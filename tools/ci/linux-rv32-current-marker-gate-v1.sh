#!/usr/bin/env bash
# Existing seven-shard RV32I campaign: exact last commit, canonical config,
# explicit legacy C-worker route, and no concurrent RV64 campaign marker.
set -Eeuo pipefail

active=false
marker=tools/ci/linux-rv32-current-runtime-trigger.txt
if [[ "${GITHUB_EVENT_NAME:-}" == push &&
      "${GITHUB_REF:-}" == refs/heads/refactor/minic-target-language-boundary ]] &&
   git rev-parse --verify HEAD^ >/dev/null 2>&1 &&
   ! git diff --quiet HEAD^ HEAD -- "$marker" &&
   [[ -f "$marker" ]] &&
   grep -Fxq 'target=rv32i' "$marker" &&
   grep -Fxq 'mode=original-1741-current-head' "$marker" &&
   grep -Fxq 'gnu_config_sha256=4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c' "$marker" &&
   grep -Eq '^nonce=20261107-[1-9][0-9]*$' "$marker" &&
   ( [[ "$(wc -l < "$marker")" -eq 4 ]] ||
     ( [[ "$(wc -l < "$marker")" -eq 5 ]] &&
       tail -n 1 "$marker" | grep -Eq '^resume_run=[1-9][0-9]{7,14}$' ) ) &&
   git log -1 --format=%B | grep -Fq '[linux-rv32i-kbuild]' &&
   git diff --quiet HEAD^ HEAD -- \
     tools/ci/linux-rv64-runtime-trigger.txt \
     tools/ci/linux-rv64-p1-eight-trigger.txt; then
  if [[ "$(git rev-parse HEAD)" != "${GITHUB_SHA:-}" ]]; then
    echo 'RV32I_CURRENT_EXACT_HEAD_GATE=FAIL unexpected SHA mismatch' >&2
    exit 1
  fi
  active=true
fi
# A tagged push on the campaign branch is an explicit run request. An absent,
# malformed, or stale exact-HEAD marker must FAIL, never produce gate-only green.
if [[ "$active" == false && "${GITHUB_EVENT_NAME:-}" == push &&
      "${GITHUB_REF:-}" == refs/heads/refactor/minic-target-language-boundary ]] &&
   git log -1 --format=%B | grep -Fq '[linux-rv32i-kbuild]'; then
  echo 'RV32I_CURRENT_EXACT_HEAD_GATE=FAIL tagged request missing valid current-HEAD marker' >&2
  exit 1
fi
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  printf 'run=%s\n' "$active" >> "$GITHUB_OUTPUT"
fi
echo "RV32I_CURRENT_EXACT_HEAD_GATE=$active sha=$(git rev-parse HEAD) config=4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c"
