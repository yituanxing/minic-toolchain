#!/usr/bin/env bash
# Opt-in current-production Symbolic source-scoped CALL A/B.
# Reuses existing perf500 job, frozen first500, runtime builder and paired runner.
# Do not run against canonical production checkout: applies a temporary C patch.
set -Eeuo pipefail
root="${1:?perf evidence root}"
corpus="${2:?SHA-locked Linux first500 corpus}"
trial=symbolic-source-call
out="$root/$trial-ab"
mkdir -p "$root/evidence" "$out"

test "$(wc -l < "$corpus/inputs.sha256")" -eq 500
sha256sum --status --check "$corpus/inputs.sha256"
git diff --exit-code -- src/compiler/compiler.c
export MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1
bash tools/ci/linux-runtime-build-minic-profile-v1.sh \
  "$root/$trial-before" "$root/evidence/$trial-before"
python3 tools/ci/apply-perf-symbolic-source-bucket-calls-v1.py
git diff --check
test "$(git diff --name-only -- src include)" = src/compiler/compiler.c
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$root/$trial-after" \
  "$root/$trial-after/bin/minic" "$root/$trial-after/bin/minic-cc" >/dev/null
base="$root/$trial-before/bin/minic"
candidate="$root/$trial-after/bin/minic"
base_cc="$root/$trial-before/bin/minic-cc"
candidate_cc="$root/$trial-after/bin/minic-cc"
test -x "$base" && test -x "$candidate"
! cmp -s "$base_cc" "$candidate_cc"
sha256sum "$base_cc" "$candidate_cc" > "$root/evidence/$trial-compiler-shas.txt"

python3 - "$corpus" "$root/evidence/$trial-inputs.tsv" <<'PY'
from pathlib import Path
import sys
corpus=Path(sys.argv[1]).resolve()
target=Path(sys.argv[2])
entries={}
for line in (corpus/"selected-tus.txt").read_text().splitlines():
    fields=line.split("\t", 3)
    if len(fields)!=4:raise SystemExit("invalid Kbuild manifest")
    entries[fields[1]]=fields[2]
if len(entries)!=500:raise SystemExit("not the fixed 500 corpus")
owners=("kernel/events/core.o", "kernel/bpf/btf.o",
        "mm/memcontrol.o", "fs/ext4/fsync.o", "kernel/printk/sysctl.o")
with target.open("w") as f:
    for owner in owners:
        if owner not in entries:raise SystemExit("missing owner "+owner)
        source=(corpus/"kbuild"/entries[owner]).resolve()
        if not source.is_relative_to(corpus/"kbuild") or not source.is_file():
            raise SystemExit("invalid frozen path "+str(source))
        print(owner+"\t"+str(source), file=f)
PY

while IFS="$(printf '\t')" read -r owner frozen; do
  safe="${owner//\//_}"
  safe="${safe%.o}"
  python3 tools/ci/linux-perf-pinned-single-paired-ab-v1.py \
    --input "$frozen" --baseline-driver "$base" \
    --candidate-driver "$candidate" --repeats 4 \
    --out "$out/$safe" | tee "$root/evidence/$trial-$safe.txt"
  grep -Fq 'MINIC_ONE_OWNER_AB=PASS' "$root/evidence/$trial-$safe.txt"
  for arm in before after; do
    if [[ "$arm" == before ]]; then executable="$base"; else executable="$candidate"; fi
    /usr/bin/time -v -o "$out/$safe/$arm-rss.txt" \
      "$executable" -S "$frozen" -o "$out/$safe/$arm-rss.s" \
      >"$out/$safe/$arm.stdout" 2>"$out/$safe/$arm.stderr"
  done
  cmp "$out/$safe/before-rss.s" "$out/$safe/after-rss.s"
done < "$root/evidence/$trial-inputs.tsv"

python3 - "$out" "$root/evidence/$trial-summary.json" <<'PY'
from pathlib import Path
import json,sys,statistics
out=Path(sys.argv[1]); dest=Path(sys.argv[2])
rows=[]
for folder in sorted(out.iterdir()):
    p=folder/"paired-result.json"
    if not p.is_file():continue
    d=json.loads(p.read_text())
    assert d["assembly_identical"] and d["repeat_per_arm"]==4
    row={"owner":folder.name,
         "before_ms":d["baseline_median_ms"],
         "after_ms":d["candidate_median_ms"]}
    for mode in ("before","after"):
        z=[r for r in d["records"] if r["mode"]==mode]
        for metric in ("wall_ms","user_ms","sys_ms"):
            values=[t[metric] for t in z]
            med=statistics.median(values)
            row[mode+"_"+metric+"_p50"]=med
            row[mode+"_"+metric+"_mad"]=statistics.median(abs(v-med) for v in values)
        rss=folder/(mode+"-rss.txt")
        data=[l for l in rss.read_text().splitlines()
              if "Maximum resident set size" in l]
        if len(data)!=1:raise SystemExit("missing peak RSS "+str(rss))
        row[mode+"_rss_kib"]=int(data[0].rsplit(":",1)[1].strip())
    rows.append(row)
assert len(rows)==5
before=sum(r["before_ms"] for r in rows)
after=sum(r["after_ms"] for r in rows)
speedup=before/after
wins=sum(r["after_ms"]<r["before_ms"] for r in rows)
result={"scope":"five pinned Linux TUs; paired same runner, 4 measured runs per arm",
        "not_linux_2064_or_qemu":True,"rows":rows,
        "sum_before_p50_ms":before,"sum_after_p50_ms":after,
        "sum_speedup":speedup,"wins":wins,
        "decision":"SCREEN_PROMISING_ONLY" if speedup>=1.05 and wins>=4 else "NOT_PROMOTED_STOP"}
dest.write_text(json.dumps(result,indent=2)+"\n")
print("MINIC_SYMBOLIC_SOURCE_BUCKET_AB=PASS owners=5 identical=5 "
      "base_sum_ms=%.3f next_sum_ms=%.3f speedup=%.5f wins=%d decision=%s" %
      (before,after,speedup,wins,result["decision"]))
PY
