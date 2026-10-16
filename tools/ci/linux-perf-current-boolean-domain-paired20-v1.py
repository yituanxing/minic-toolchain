#!/usr/bin/env python3
"""Paired A/B of the EXISTING Boolean Domain cache over exactly 20 frozen TUs.

Never patches production code; the workflow separately builds the native
baseline and a disposable Boolean Domain indexed candidate. Each of the
20 real inputs runs the preexisting alternating-order paired benchmark.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for data in iter(lambda: f.read(1048576), b""):
            h.update(data)
    return h.hexdigest()


def main():
    cli = argparse.ArgumentParser()
    for name in ("corpus","selection","baseline","candidate","out"):
        cli.add_argument("--"+name,type=Path,required=True)
    args=cli.parse_args()
    manifest={}
    for line in (args.corpus/"selected-tus.txt").read_text().splitlines():
        cols=line.split("\t",3)
        if len(cols)!=4: raise RuntimeError("bad historical manifest")
        idx,owner,relative,_=cols
        if owner in manifest: raise RuntimeError("duplicate historical owner "+owner)
        manifest[owner]=(int(idx),relative)
    if len(manifest)!=500: raise RuntimeError("not the certified 500 input manifest")
    known=[]
    for line in args.selection.read_text().splitlines():
        if not line or line.lstrip().startswith("#"): continue
        cols=line.split("\t")
        if len(cols)!=3 or cols[0] not in ("hot","middle","light"):
            raise RuntimeError("invalid stratified sample")
        group,owner,prior=cols
        if owner not in manifest: raise RuntimeError("missing certified owner "+owner)
        known.append((group,owner,manifest[owner][1],float(prior)))
    if len(known)!=20 or len({x[1] for x in known})!=20: raise RuntimeError("not 20 distinct owners")
    groups={name:sum(x[0]==name for x in known) for name in ("hot","middle","light")}
    if groups!={"hot":10,"middle":5,"light":5}:raise RuntimeError("sample group drift")
    old, new = args.baseline.resolve(),args.candidate.resolve()
    for driver in (old,new,old.parent/"minic-cc",new.parent/"minic-cc"):
        if not driver.is_file():raise RuntimeError("missing actual compiler "+str(driver))
    old_sha, new_sha=sha(old.parent/"minic-cc"),sha(new.parent/"minic-cc")
    if old_sha==new_sha:raise RuntimeError("identical MiniC compiler in A/B")
    args.out.mkdir(parents=True,exist_ok=True)
    rows=[]
    for group,owner,rel,prior in known:
        source=(args.corpus/"kbuild"/rel).resolve()
        if not source.is_relative_to((args.corpus/"kbuild").resolve()) or not source.is_file():
            raise RuntimeError("missing certified .i "+rel)
        target=args.out/owner.replace("/","__").replace(".o","")
        run=subprocess.run([sys.executable,
            "tools/ci/linux-perf-pinned-single-paired-ab-v1.py",
            "--input",str(source),"--baseline-driver",str(old),
            "--candidate-driver",str(new),"--repeats","4","--out",str(target)],
            capture_output=True,text=True,errors="replace",timeout=1300)
        if run.returncode:
            raise RuntimeError("A/B failed for "+owner+": "+(run.stderr+run.stdout)[-2000:])
        data=json.loads((target/"paired-result.json").read_text())
        if data["repeat_per_arm"]!=4 or not data["assembly_identical"]:
            raise RuntimeError("failed assembly or paired repetition "+owner)
        before=data["baseline_median_ms"]
        after=data["candidate_median_ms"]
        row={"group":group,"owner":owner,"input":rel,
             "old_median_ms":before,"new_median_ms":after,
             "speedup":before/after,"input_sha256":data["input_sha256"],
             "assembly_sha256":data["assembly_sha256"],"old_historical_ms":prior}
        rows.append(row)
        print("MINIC_BOOLEAN_DOMAIN_AB20_PROGRESS completed=%d/20 owner=%s old_ms=%.3f new_ms=%.3f speedup=%.3fx"%
              (len(rows),owner,before,after,before/after),flush=True)
    total_old=sum(x["old_median_ms"] for x in rows)
    total_new=sum(x["new_median_ms"] for x in rows)
    result={"schema":"minic-current-boolean-domain-cache20-paired-v1",
            "baseline_cc_sha256":old_sha,"candidate_cc_sha256":new_sha,
            "inputs":20,"groups":groups,"all_assembly_identical":True,
            "same_runner":True,"four_alternating_repeats_each":True,
            "baseline_aggregate_median_ms":total_old,
            "candidate_aggregate_median_ms":total_new,
            "aggregate_speedup":total_old/total_new,
            "by_group":{g:{"baseline_ms":sum(x["old_median_ms"] for x in rows if x["group"]==g),
                           "candidate_ms":sum(x["new_median_ms"] for x in rows if x["group"]==g)}
                       for g in groups},"rows":rows}
    (args.out/"boolean-domain-paired20.json").write_text(json.dumps(result,indent=2)+"\n")
    print("MINIC_BOOLEAN_DOMAIN_AB20=PASS owners=20 assembly_identical=20/20 "
          "baseline_aggregate_median_ms=%.3f candidate_aggregate_median_ms=%.3f speedup=%.4fx "
          "scope=stratified_20_not_full_2064"%(total_old,total_new,total_old/total_new),flush=True)
    for group,nums in result["by_group"].items():
        print("MINIC_BOOLEAN_DOMAIN_AB20_GROUP group=%s old_ms=%.3f new_ms=%.3f speedup=%.4fx"%
              (group,nums["baseline_ms"],nums["candidate_ms"],
               nums["baseline_ms"]/nums["candidate_ms"]),flush=True)


if __name__=="__main__":
    main()
