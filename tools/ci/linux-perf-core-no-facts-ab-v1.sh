#!/usr/bin/env bash
# Source-unchanged baseline versus no-specialization-facts Core candidate.
# GNU Kbuild first500 SHA-pinned inputs; existing same-runner paired script.
set -Eeuo pipefail
root="${1:?audit root}"
corpus="${2:?frozen corpus}"
trial=core-no-facts
out="$root/$trial-ab"
mkdir -p "$out" "$root/evidence"
test "$(wc -l < "$corpus/inputs.sha256")" -eq 500
sha256sum --status --check "$corpus/inputs.sha256"
git diff --exit-code -- src/core/core_lower.c
export MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1
bash tools/ci/linux-runtime-build-minic-profile-v1.sh \
  "$root/$trial-before" "$root/evidence/$trial-before"
python3 tools/ci/apply-perf-core-specialization-no-facts-fastpath-v1.py
git diff --check
test "$(git diff --name-only -- src include)" = src/core/core_lower.c
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$root/$trial-after" \
  "$root/$trial-after/bin/minic" "$root/$trial-after/bin/minic-cc" \
  check-type check-ast-contract >"$root/evidence/$trial-build.log" 2>&1 || {
  tail -n 90 "$root/evidence/$trial-build.log";exit 23;
}
base="$root/$trial-before/bin/minic"
next="$root/$trial-after/bin/minic"
! cmp -s "$root/$trial-before/bin/minic-cc" "$root/$trial-after/bin/minic-cc"
sha256sum "$root/$trial-before/bin/minic-cc" "$root/$trial-after/bin/minic-cc" \
  >"$root/evidence/$trial-compiler-shas.txt"
if [[ "${MINIC_CORE_NO_FACTS_BUILD_ONLY:-0}" == 1 ]]; then
  echo "MINIC_CORE_NO_FACTS_PAIR_READY=PASS release_original_not_modified=true"
  exit 0
fi
python3 - "$corpus" "$root/evidence/$trial-inputs.tsv" <<'PY'
from pathlib import Path
import sys
corpus=Path(sys.argv[1]).resolve()
out=Path(sys.argv[2])
entries={}
for line in (corpus/"selected-tus.txt").read_text().splitlines():
    cols=line.split("\t",3)
    if len(cols)!=4:raise SystemExit("bad frozen 500 manifest")
    entries[cols[1]]=cols[2]
if len(entries)!=500:raise SystemExit("expected 500 entries")
owners=("fs/ext4/fsync.o","kernel/bpf/btf.o","kernel/events/core.o","mm/memcontrol.o")
with out.open("w") as fd:
    for owner in owners:
        if owner not in entries:raise SystemExit("missing selected owner "+owner)
        src=(corpus/"kbuild"/entries[owner]).resolve()
        if not src.is_relative_to(corpus/"kbuild") or not src.is_file():
            raise SystemExit("missing trusted Kbuild input")
        print(owner+"\t"+str(src),file=fd)
PY
while IFS="$(printf '\t')" read -r owner source; do
  safe="${owner//\//_}"
  safe="${safe%.o}"
  python3 tools/ci/linux-perf-pinned-single-paired-ab-v1.py \
    --input "$source" --baseline-driver "$base" --candidate-driver "$next" \
    --repeats 4 --out "$out/$safe" \
    | tee "$root/evidence/$trial-$safe.txt"
  grep -Fq 'MINIC_ONE_OWNER_AB=PASS' "$root/evidence/$trial-$safe.txt"
  for arm in before after; do
    if [[ "$arm" == before ]]; then exe="$base"; else exe="$next"; fi
    /usr/bin/time -v -o "$out/$safe/$arm-rss.txt" \
      "$exe" -S "$source" -o "$out/$safe/$arm-rss.s" \
      >"$out/$safe/$arm.stdout" 2>"$out/$safe/$arm.stderr"
  done
  cmp "$out/$safe/before-rss.s" "$out/$safe/after-rss.s"
done <"$root/evidence/$trial-inputs.tsv"
python3 - "$out" "$root/evidence/$trial-summary.json" <<'PY'
import json,sys,statistics
from pathlib import Path
root=Path(sys.argv[1]);dest=Path(sys.argv[2])
rows=[]
for folder in sorted(root.iterdir()):
    data=folder/"paired-result.json"
    if not data.exists():continue
    d=json.loads(data.read_text())
    assert d["assembly_identical"] and d["repeat_per_arm"]==4
    row={"owner":folder.name,"base_ms":d["baseline_median_ms"],
         "candidate_ms":d["candidate_median_ms"]}
    for arm in ("before","after"):
        xs=[x for x in d["records"] if x["mode"]==arm]
        row[arm+"_cpu_p50_ms"]=statistics.median(x["user_ms"]+x["sys_ms"] for x in xs)
        rss=[l for l in (folder/(arm+"-rss.txt")).read_text().splitlines()
             if "Maximum resident set size" in l]
        if len(rss)!=1:raise SystemExit("missing RSS")
        row[arm+"_rss_kib"]=int(rss[0].rsplit(":",1)[1].strip())
    rows.append(row)
assert len(rows)==4
before=sum(r["base_ms"] for r in rows)
after=sum(r["candidate_ms"] for r in rows)
wins=sum(r["candidate_ms"]<r["base_ms"] for r in rows)
speedup=before/after
result={"scope":"four pinned real Linux owner paired A/B, 4 repetitions per arm",
        "rows":rows,"before_ms":before,"after_ms":after,"speedup":speedup,
        "wins":wins,"not_production_or_2064_qemu":True,
        "decision":"PROMISING_FOR_500" if speedup>=1.04 and wins>=3 else "NOT_PROMOTED_STOP"}
dest.write_text(json.dumps(result,indent=2)+"\n")
print("MINIC_CORE_NO_FACTS_AB=PASS owners=4 equal=4/4 "
      "before_p50_sum_ms=%.3f after_p50_sum_ms=%.3f speedup=%.5f wins=%d decision=%s" %
      (before,after,speedup,wins,result["decision"]))
PY
