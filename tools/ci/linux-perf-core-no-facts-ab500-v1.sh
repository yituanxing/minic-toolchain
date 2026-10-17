#!/usr/bin/env bash
# Promote only the SCREENING SCOPE of Core no-facts to immutable first500.
# Does not change production C or build full Linux. Reuses official paired diff.
set -Eeuo pipefail
root="${1:?audit root}"
corpus="${2:?frozen Linux corpus}"
trial=core-no-facts
mkdir -p "$root/evidence"
test "$(wc -l < "$corpus/inputs.sha256")" -eq 500
sha256sum --status --check "$corpus/inputs.sha256"
MINIC_CORE_NO_FACTS_BUILD_ONLY=1 \
  bash tools/ci/linux-perf-core-no-facts-ab-v1.sh "$root" "$corpus" \
  | tee "$root/evidence/core-no-facts-compiler-pair.txt"
grep -Fq 'MINIC_CORE_NO_FACTS_PAIR_READY=PASS' \
  "$root/evidence/core-no-facts-compiler-pair.txt"
python3 tools/ci/linux-perf-frozen-corpus-differential-v1.py \
  --corpus "$corpus" \
  --baseline-driver "$root/$trial-before/bin/minic" \
  --candidate-driver "$root/$trial-after/bin/minic" \
  --jobs 4 --balanced-order \
  --out "$root/$trial-first500" \
  | tee "$root/evidence/$trial-first500-verdict.txt"
grep -Fq 'MINIC_FROZEN_DIFF=PASS selected=500 identical=500' \
  "$root/evidence/$trial-first500-verdict.txt"
grep -Fq 'MINIC_FROZEN_PAIRED=PASS owners=500 balanced_order=true' \
  "$root/evidence/$trial-first500-verdict.txt"
python3 - "$root/$trial-first500/differential.json" "$root/evidence/$trial-first500-summary.json" <<'PY'
from pathlib import Path
import json,sys
d=json.loads(Path(sys.argv[1]).read_text())
rows=d["results"]
if len(rows)!=500 or d["identical"]!=500 or d["mismatch_count"]!=0:
    raise SystemExit("missing trusted full500 or output divergence")
before=sum(x["baseline"]["wall_ms"] for x in rows)
after=sum(x["candidate"]["wall_ms"] for x in rows)
if before<=0 or after<=0:raise SystemExit("invalid timing")
worst=sorted(rows,key=lambda x:x["candidate"]["wall_ms"]-x["baseline"]["wall_ms"],reverse=True)
data={"scope":"immutable GNU Kbuild first500, same runner 4 parallel workers balanced",
      "no_2064_QEMU":True,"all_assembly_identical":True,
      "baseline_total_tu_wall_ms":before,"candidate_total_tu_wall_ms":after,
      "speedup":before/after,"regressed_single_run_count":
      sum(x["candidate"]["wall_ms"]>x["baseline"]["wall_ms"] for x in rows),
      "worst_regressions":[{
          "owner":x["object"],"baseline_ms":x["baseline"]["wall_ms"],
          "candidate_ms":x["candidate"]["wall_ms"]} for x in worst[:15]]}
data["decision"]="CHECK_SAFETY_AND_TRIAGE" if before/after>=1.04 else "NOT_PROMOTED_STOP"
Path(sys.argv[2]).write_text(json.dumps(data,indent=2)+"\n")
print("MINIC_CORE_NO_FACTS_FIRST500=PASS equal=500/500 "
      "base_sum_ms=%.3f candidate_sum_ms=%.3f speedup=%.5f "
      "regressions=%d decision=%s" % (
          before,after,before/after,data["regressed_single_run_count"],data["decision"]))
PY
