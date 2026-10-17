#!/usr/bin/env python3
"""Recheck observed frozen500 packed-CALL single-pass outliers on the same 2 binaries.

8 old and 8 new runs per owner (two 4+4 paired batches). Report wall/user/sys,
MAD and classification without assuming that one concurrent-CI observation
represents a persistent compiler regression.
"""
from __future__ import annotations
import argparse
import json
import statistics
import subprocess
from pathlib import Path

OWNERS=(
    "fs/nfs/client.o",
    "fs/nfs/blocklayout/extent_tree.o",
    "init/version.o",
    "init/do_mounts_initrd.o",
    "kernel/sys_ni.o",
    "kernel/rcu/sync.o",
    "arch/riscv/purgatory/ctype.o",
    "kernel/bpf/btf.o", # large positive control
    "fs/ext4/fsync.o", # medium positive control
)

def median_mad(rows, key):
    vals=[float(row[key]) for row in rows]
    mid=statistics.median(vals)
    return mid,statistics.median(abs(x-mid) for x in vals)

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--corpus",type=Path,required=True)
    p.add_argument("--baseline-driver",type=Path,required=True)
    p.add_argument("--candidate-driver",type=Path,required=True)
    p.add_argument("--out",type=Path,required=True)
    p.add_argument("--owner",dest="owners",action="append",default=None,
                   help="Optional owner list; default preserves original packed-call 9-owner trial")
    p.add_argument("--label",default="MINIC_PACKED_REGRESSION")
    a=p.parse_args()
    if not a.label.replace("_", "").isalnum() or a.label != a.label.upper():
        raise SystemExit("invalid report label")
    owners=tuple(a.owners) if a.owners else OWNERS
    if len(set(owners))!=len(owners): raise SystemExit("duplicate owners")
    a.out.mkdir(parents=True,exist_ok=True)
    selected={}
    for line in (a.corpus/"selected-tus.txt").read_text().splitlines():
        cols=line.split("\t",3)
        if len(cols)!=4: raise SystemExit("malformed selected-tus row")
        selected[cols[1]]=cols[2]
    if len(selected)!=500: raise SystemExit("first500 corpus identity mismatch")
    rows=[]
    for owner in owners:
        if owner not in selected: raise SystemExit("owner missing from frozen500 "+owner)
        source=(a.corpus/"kbuild"/selected[owner]).resolve()
        if not source.is_file() or not source.is_relative_to((a.corpus/"kbuild").resolve()):
            raise SystemExit("invalid pinned source path "+owner)
        safe=owner.replace("/", "_").removesuffix(".o")
        work=a.out/safe
        samples=[]
        for batch in ("a","b"):
            report=work/batch
            cmd=("python3","tools/ci/linux-perf-pinned-single-paired-ab-v1.py",
                 "--input",str(source),"--baseline-driver",str(a.baseline_driver),
                 "--candidate-driver",str(a.candidate_driver),
                 "--out",str(report),"--repeats","4")
            subprocess.run(cmd,check=True,capture_output=False)
            doc=json.loads((report/"paired-result.json").read_text())
            if not doc["assembly_identical"] or doc["repeat_per_arm"]!=4:
                raise SystemExit("inconsistent generated .s "+owner)
            samples.append(doc)
        if samples[0]["assembly_sha256"]!=samples[1]["assembly_sha256"]:
            raise SystemExit("assembly differs across repeat batches "+owner)
        old=[r for d in samples for r in d["records"] if r["mode"]=="before"]
        new=[r for d in samples for r in d["records"] if r["mode"]=="after"]
        if len(old)!=8 or len(new)!=8: raise SystemExit("not 8+8 samples "+owner)
        before,b_mad=median_mad(old,"wall_ms")
        after,a_mad=median_mad(new,"wall_ms")
        old_user,old_cpu_mad=median_mad(old,"user_ms")
        new_user,new_cpu_mad=median_mad(new,"user_ms")
        old_sys,_=median_mad(old,"sys_ms")
        new_sys,_=median_mad(new,"sys_ms")
        delta=after-before
        suspect_wall=delta>max(5.,2.5*(b_mad+a_mad),.03*before)
        suspect_cpu=(new_user-old_user)>max(5.,.03*old_user)
        cls=("reproduced_wall_and_cpu_slowdown_suspect" if suspect_wall and suspect_cpu
             else "mixed_or_noisy" if delta>0 else "not_reproduced")
        item=dict(owner=owner,old_wall_ms=round(before,3),new_wall_ms=round(after,3),
                  old_user_ms=round(old_user,3),new_user_ms=round(new_user,3),
                  old_sys_ms=round(old_sys,3),new_sys_ms=round(new_sys,3),
                  old_mad_ms=round(b_mad,3),new_mad_ms=round(a_mad,3),
                  old_cpu_mad_ms=round(old_cpu_mad,3),
                  new_cpu_mad_ms=round(new_cpu_mad,3),
                  classification=cls,assembly_sha256=samples[0]["assembly_sha256"])
        print(a.label+"_OWNER="+json.dumps(item,separators=(',',':')),flush=True)
        (work/"regression.json").write_text(json.dumps(item,indent=2)+"\n")
        rows.append(item)
    if len(rows)!=len(owners): raise SystemExit("incomplete owner coverage")
    counts={kind:sum(row["classification"]==kind for row in rows)
            for kind in ("not_reproduced","mixed_or_noisy",
                         "reproduced_wall_and_cpu_slowdown_suspect")}
    (a.out/"summary.json").write_text(json.dumps(dict(owners=rows,counts=counts),indent=2)+"\n")
    print(a.label+"=PASS owners=%d old_new_pairs=8_each counts=%s"%
          (len(rows),json.dumps(counts,sort_keys=True)),flush=True)

if __name__=="__main__":
    main()
