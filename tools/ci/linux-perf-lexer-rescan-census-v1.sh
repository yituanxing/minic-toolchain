#!/usr/bin/env bash
# One-factor diagnostic profile: how much SAME token-start work repeats?
# Reuses SHA-pinned GNU Linux .i input and the already authenticated perf500 job.
set -Eeuo pipefail
root="${1:?audit root}"
corpus="${2:?trusted first500 corpus}"
trial=lexer-rescan-census
out="$root/$trial"
mkdir -p "$out" "$root/evidence"
test "$(wc -l < "$corpus/inputs.sha256")" -eq 500
sha256sum --status --check "$corpus/inputs.sha256"
git diff --exit-code -- src/frontend/lexer.c
export MINIC_LINUX_FULL_CROSSBLOCK=1 MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1
bash tools/ci/linux-runtime-build-minic-profile-v1.sh \
  "$root/$trial-original" "$root/evidence/$trial-original"
python3 tools/ci/apply-perf-lexer-rescan-census-v1.py
git diff --check
test "$(git diff --name-only -- src include)" = src/frontend/lexer.c
make -j4 MODE=release CFLAGS=-Werror \
  BUILD_DIR="$root/$trial-instrumented" \
  "$root/$trial-instrumented/bin/minic" "$root/$trial-instrumented/bin/minic-cc" \
  >"$root/evidence/$trial-build.txt" 2>&1 || {
    tail -n 120 "$root/evidence/$trial-build.txt";exit 22;
  }
original="$root/$trial-original/bin/minic"
profiled="$root/$trial-instrumented/bin/minic"
test -x "$original" && test -x "$profiled"
! cmp -s "$root/$trial-original/bin/minic-cc" "$root/$trial-instrumented/bin/minic-cc"
python3 - "$corpus" "$root/evidence/$trial-selected.tsv" <<'PY'
from pathlib import Path
import sys
corpus=Path(sys.argv[1]).resolve()
rows={}
for row in (corpus/"selected-tus.txt").read_text().splitlines():
    fields=row.split("\t",3)
    if len(fields)!=4:raise SystemExit("wrong SHA-frozen manifest")
    rows[fields[1]]=fields[2]
if len(rows)!=500:raise SystemExit("expected 500 selected TUs")
objects=("fs/ext4/fsync.o","kernel/bpf/btf.o","kernel/events/core.o",
         "mm/memcontrol.o","kernel/printk/sysctl.o")
base=(corpus/"kbuild").resolve()
with open(sys.argv[2],"w") as out:
    for object_name in objects:
        if object_name not in rows:raise SystemExit("missing "+object_name)
        source=(base/rows[object_name]).resolve()
        if not source.is_relative_to(base) or not source.is_file():
            raise SystemExit("invalid frozen source")
        print(object_name+"\t"+str(source),file=out)
PY

while IFS="$(printf '\t')" read -r owner src; do
    safe="${owner//\//_}"
    safe="${safe%.o}"
    "$original" -S "$src" -o "$out/$safe.original.s" \
      >"$out/$safe.original.out" 2>"$out/$safe.original.err"
    MINIC_LEXER_RESCAN_PROFILE=1 \
      "$profiled" -S "$src" -o "$out/$safe.profiled.s" \
      >"$out/$safe.profiled.out" 2>"$out/$safe.profiled.err"
    cmp "$out/$safe.original.s" "$out/$safe.profiled.s"
    grep -Eq '^MINIC_LEXER_RESCAN_CENSUS=PASS ' "$out/$safe.profiled.err"
    printf 'MINIC_LEXER_RESCAN_TU=PASS owner=%s original_assembly_equal=true\n' "$owner"
done < "$root/evidence/$trial-selected.tsv"
python3 - "$root/evidence/$trial-selected.tsv" "$out" "$root/evidence/$trial-summary.json" <<'PY'
import json,re,sys,hashlib
from pathlib import Path
selected=Path(sys.argv[1]).read_text().splitlines()
out=Path(sys.argv[2]); dest=Path(sys.argv[3])
required=("source_bytes","calls","unique_starts","repeated_starts","third_plus",
          "original_scan_bytes","repeated_scan_bytes",
          "repeated_identifier_tokens","max_visits_same_start","untracked_calls")
rows=[]
for line in selected:
    name,path=line.split("\t")
    safe=name.replace("/","_").removesuffix(".o")
    stderr=(out/(safe+".profiled.err")).read_text()
    matches=re.findall(r"^MINIC_LEXER_RESCAN_CENSUS=PASS (.*)$",stderr,re.M)
    if len(matches)!=1:raise SystemExit("missing census "+name)
    values=dict(re.findall(r"(\w+)=(\d+)",matches[0]))
    if set(values)!=set(required):raise SystemExit("invalid/incomplete census "+name)
    fields={key:int(values[key]) for key in required}
    if fields["calls"]!=fields["unique_starts"]+fields["repeated_starts"]:
        raise SystemExit("bad call count "+name)
    if fields["third_plus"]>fields["repeated_starts"] or (
        fields["untracked_calls"]>0):
        raise SystemExit("unexpected multiple input/invalid profile "+name)
    rows.append({"owner":name,"source_sha256":hashlib.sha256(Path(path).read_bytes()).hexdigest(),
                 **fields,"revisit_call_rate":fields["repeated_starts"]/max(1,fields["calls"]),
                 "revisit_bytes_rate":fields["repeated_scan_bytes"]/max(
                     1,fields["original_scan_bytes"]+fields["repeated_scan_bytes"])})
total={key:sum(row[key] for row in rows) for key in required}
total["revisit_call_rate"]=total["repeated_starts"]/total["calls"]
total["revisit_bytes_rate"]=total["repeated_scan_bytes"]/(
    total["original_scan_bytes"]+total["repeated_scan_bytes"])
dest.write_text(json.dumps({"scope":"five SHA-locked Linux .i; diagnostic instrumentation only",
                            "no_performance_speedup_claim":True,"assembly_identical":5,
                            "rows":rows,"total":total},indent=2)+"\n")
for row in rows:
    print("MINIC_LEXER_RESCAN_OWNER=PASS owner=%s calls=%d revisits=%d "
          "revisit_bytes_rate=%.4f max_visits=%d" %
          (row["owner"],row["calls"],row["repeated_starts"],
           row["revisit_bytes_rate"],row["max_visits_same_start"]))
print("MINIC_LEXER_RESCAN_FIVE=PASS equal=5/5 calls=%d repeated=%d "
      "repeat_calls_ratio=%.4f repeat_bytes_ratio=%.4f" %
      (total["calls"],total["repeated_starts"],
       total["revisit_call_rate"],total["revisit_bytes_rate"]))
PY
