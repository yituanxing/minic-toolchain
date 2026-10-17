#!/usr/bin/env bash
# Current certified source -> opt-in Core source buckets. No production edits.
# Reuse existing SHA-frozen first500 and same-runner balanced A/B oracles.
set -Eeuo pipefail
root="${1:?audit root}"
corpus="${2:?frozen first500 corpus}"
trial=core-variant-source
test "$(wc -l < "$corpus/inputs.sha256")" -eq 500
sha256sum --status --check "$corpus/inputs.sha256"
export MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1
MINIC_CORE_VARIANT_SOURCE_BUILD_ONLY=1 \
  bash tools/ci/linux-perf-core-variant-source-ab-v1.sh "$root" "$corpus"
test -x "$root/$trial-before/bin/minic"
test -x "$root/$trial-after/bin/minic"
out="$root/$trial-first500"
python3 tools/ci/linux-perf-frozen-corpus-differential-v1.py \
  --corpus "$corpus" \
  --baseline-driver "$root/$trial-before/bin/minic" \
  --candidate-driver "$root/$trial-after/bin/minic" \
  --jobs 4 --balanced-order --out "$out" \
  | tee "$root/evidence/$trial-first500-verdict.txt"
python3 - "$out/differential.json" "$root/evidence/$trial-first500-summary.json" <<'PY'
import json,sys,statistics
from pathlib import Path
d=json.loads(Path(sys.argv[1]).read_text())
rows=d["results"]
assert d["identical"]==500 and d["mismatch_count"]==0
assert d["balanced_order"] and d["baseline_cc_sha256"]!=d["candidate_cc_sha256"]
assert len(rows)==500
assert sum(r["first"]=="baseline" for r in rows)==250
assert sum(r["first"]=="candidate" for r in rows)==250
before=sum(r["baseline"]["wall_ms"] for r in rows)
after=sum(r["candidate"]["wall_ms"] for r in rows)
regressions=sorted((r for r in rows if r["candidate"]["wall_ms"]>r["baseline"]["wall_ms"]),
                   key=lambda r:r["candidate"]["wall_ms"]-r["baseline"]["wall_ms"],reverse=True)
speedup=before/after
gate="PROMISING_FOR_TRIAGE" if speedup>=1.04 else "NOT_PROMOTED_STOP"
summary={"before_ms":before,"after_ms":after,"speedup":speedup,
         "regressions_single_run":len(regressions),
         "top_single_run_regressions":[
             {"owner":r["object"],"before_ms":r["baseline"]["wall_ms"],
              "after_ms":r["candidate"]["wall_ms"]} for r in regressions[:20]],
         "decision":gate,"scope":"balanced first500 GNU Kbuild .i MiniC -S not 2064/QEMU"}
Path(sys.argv[2]).write_text(json.dumps(summary,indent=2)+"\n")
print("MINIC_CORE_VARIANT_SOURCE_AB500=PASS owners=500 equal=500/500 "
      "old_total_ms=%.3f new_total_ms=%.3f speedup=%.5fx "
      "regressions=%d decision=%s"%(
          before,after,speedup,len(regressions),gate))
PY
