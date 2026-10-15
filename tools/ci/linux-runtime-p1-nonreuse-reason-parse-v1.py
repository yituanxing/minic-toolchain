#!/usr/bin/env python3
"""Fail-closed attribution of every P1 CoreObject's permanent-slot reason."""
import csv
import re
import sys
from collections import Counter
from pathlib import Path

RX=re.compile(r"^CORE_FRAME_REASON function=(\S+) id=(\d+) reason=(\w+)$")

def run(raw: str, objects: str, intervals: str) -> str:
    obj={(r["function"],int(r["id"])):r
         for r in csv.DictReader(objects.splitlines(),delimiter="\t")}
    live={(r["function"],int(r["id"])):r
          for r in csv.DictReader(intervals.splitlines(),delimiter="\t")}
    got={}
    for line in raw.splitlines():
        m=RX.fullmatch(line)
        if not m: continue
        name,i,reason=m.groups()
        key=name,int(i)
        if key in got: raise ValueError(f"duplicate reason {key}")
        got[key]=reason
    if not obj or obj.keys()!=got.keys() or live.keys()!=got.keys():
        raise ValueError(
            f"coverage mismatch objects={len(obj)} reasons={len(got)} intervals={len(live)}")
    counts=Counter()
    for key,reason in got.items():
        reused=live[key]["reusable"]=="1"
        if reused != (reason=="reusable"):
            raise ValueError(f"reason disagrees with runtime-reuse analysis: {key} {reason}")
        counts[(key[0],reason)] += 1
    byfun=Counter(k[0] for k in obj)
    for fn,total in sorted(byfun.items()):
        measured=sum(n for (f,r),n in counts.items() if f==fn)
        if measured!=total:raise ValueError("bad category sum "+fn)
        permanent=sum(n for (f,r),n in counts.items() if f==fn and r!="reusable")
        print(f"P1_FRAME_NONREUSE=PASS function={fn} objects={total} permanent={permanent} "
              f"reusable={total-permanent}")
    rows=["function\treason\tcount"]
    for (fn,reason),n in sorted(counts.items(),key=lambda p:(p[0][0],-p[1],p[0][1])):
        print(f"P1_FRAME_NONREUSE_REASON function={fn} reason={reason} count={n}")
        rows.append(f"{fn}\t{reason}\t{n}")
    return "\n".join(rows)+"\n"

if __name__=="__main__":
    if len(sys.argv)!=5:raise SystemExit("usage: parser.py <replay.log> <objects.tsv> <intervals.tsv> <reasons.tsv>")
    log,obj,inter,output=map(Path,sys.argv[1:])
    output.write_text(run(log.read_text(errors="replace"),obj.read_text(),inter.read_text()))
