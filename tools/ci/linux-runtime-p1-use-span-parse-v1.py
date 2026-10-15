#!/usr/bin/env python3
"""Correlate pinned-MiniC pointer comparisons and stored addresses with .i spans.

Validates source-span audit completeness, aggregates hot origins. Observational
only: the 3 emitted .o must be SHA-identical to the frozen 2064-object pool.
"""
from __future__ import annotations
import csv
import re
import sys
from collections import Counter,defaultdict
from pathlib import Path

OBJ=re.compile(r"^CORE_FRAME_OBJ_SPAN function=(\S+) id=(\d+) line=(\d+) offset=(\d+)$")
USE=re.compile(r"^CORE_FRAME_USE_SPAN function=(\S+) id=(\d+) kind=(\d+) line=(\d+) offset=(\d+)$")
CONSUMER=re.compile(r"^CORE_FRAME_DERIVED_USE function=(\S+) id=(\d+) category=(\w+) kind=(\d+) n=(\d+)$")

def analyze(data:str,object_tsv:str)->tuple[str,str]:
    expected={(r["function"],int(r["id"])) for r in csv.DictReader(object_tsv.splitlines(),delimiter="\t")}
    obj={}
    consumers=[]
    uses=[]
    for line in data.splitlines():
        if match:=OBJ.fullmatch(line):
            function,oid,linenr,offset=match.groups()
            key=function,int(oid)
            if key in obj:
                raise ValueError(f"duplicate object provenance {key}")
            obj[key]=(int(linenr),int(offset))
        if match:=CONSUMER.fullmatch(line):
            function,oid,category,kind,n=match.groups()
            if category!="terminator":
                consumers.append((function,int(oid),int(kind),category,int(n)))
        if match:=USE.fullmatch(line):
            function,oid,kind,linenr,offset=match.groups()
            uses.append((function,int(oid),int(kind),int(linenr),int(offset)))
    if expected!=obj.keys():
        raise ValueError(f"object provenance coverage expected={len(expected)} got={len(obj)}")
    if len(uses)!=len(consumers) or not consumers:
        raise ValueError(f"consumer provenance coverage expected={len(consumers)} got={len(uses)}")
    counter=Counter()
    lines=["function\tid\tobject_line\tobject_offset\tkind\tcategory\tuse_line\tuse_offset\tn"]
    for (fn,oid,kind,cat,n),(ufn,uid,ukind,line,offset) in zip(consumers,uses):
        if (fn,oid,kind)!=(ufn,uid,ukind):
            raise ValueError(f"consumer span mismatched at {fn}:{oid}:{kind}")
        if (fn,oid) not in obj:
            raise ValueError("unknown traced object")
        objline,objoff=obj[fn,oid]
        lines.append(f"{fn}\t{oid}\t{objline}\t{objoff}\t{kind}\t{cat}\t{line}\t{offset}\t{n}")
        counter[(fn,cat,line)] += n
    hot=["function\tcategory\tuse_line\tcount"]
    for (fn,cat,line),n in sorted(counter.items(),key=lambda x:(x[0][0],-x[1],x[0][2])):
        hot.append(f"{fn}\t{cat}\t{line}\t{n}")
    print(f"P1_FRAME_USE_SPAN=PASS objects={len(obj)} consumers={len(consumers)} distinct_sites={len(counter)}")
    for (fn,cat,line),n in Counter(counter).most_common(15):
        print(f"P1_FRAME_USE_HOTSPOT function={fn} category={cat} source_line={line} count={n}")
    return "\n".join(lines)+"\n","\n".join(hot)+"\n"

if __name__=="__main__":
    if len(sys.argv)!=5:
        raise SystemExit("usage: source_spans.py <replay.log> <objects.tsv> <per-use.tsv> <hotspots.tsv>")
    raw,objects,detail,hotspot=map(Path,sys.argv[1:])
    per,hot=analyze(raw.read_text(errors="replace"),objects.read_text())
    detail.write_text(per)
    hotspot.write_text(hot)
