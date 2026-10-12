#!/usr/bin/env python3
"""Summarize the exact IR consumers of otherwise permanent pointer objects."""
import csv
import re
import sys
from collections import Counter,defaultdict
from pathlib import Path

RX=re.compile(
    r"^CORE_FRAME_DERIVED_USE function=(\S+) id=(\d+) "
    r"category=(\w+) kind=(\d+) n=(\d+)$"
)
if len(sys.argv)!=4:
    raise SystemExit("usage: derived.py replay.log nonreuse-reasons.tsv derived-use.tsv")
log,reason_file,dest=map(Path,sys.argv[1:])
reasons={r["function"]:int(r["count"]) for r in csv.DictReader(
    reason_file.read_text().splitlines(),delimiter="\t")
    if r["reason"]=="address_derived_or_escaped"}
hits=Counter()
owners=defaultdict(set)
for line in log.read_text(errors="replace").splitlines():
    m=RX.fullmatch(line)
    if not m:continue
    fn,idx,category,kind,n=m.groups()
    if fn not in reasons:
        raise SystemExit(f"unexpected function in derived-use capture: {fn}")
    hits[(fn,category)] += int(n)
    owners[(fn,category)].add(int(idx))
if not hits:
    raise SystemExit("P1_FRAME_DERIVED=ERROR no derived-use records")
rows=["function\tcategory\tinstruction_uses\tobject_count"]
for key,n in sorted(hits.items(),key=lambda x:(x[0][0],-x[1],x[0][1])):
    fn,category=key
    count=len(owners[key])
    if count>reasons[fn]:
        raise SystemExit("P1_FRAME_DERIVED=ERROR use count exceeds rejected objects")
    print(f"P1_FRAME_DERIVED=COUNT function={fn} category={category} "
          f"uses={n} distinct_objects={count}")
    rows.append(f"{fn}\t{category}\t{n}\t{count}")
dest.write_text("\n".join(rows)+"\n")
print(f"P1_FRAME_DERIVED=PASS functions={len({x[0] for x in hits})} categories={len(hits)}")
