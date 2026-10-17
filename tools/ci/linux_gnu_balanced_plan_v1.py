#!/usr/bin/env python3
"""Immutable, SHA-bound GNU Linux 2064 owner mapping for seven CI shards."""
import hashlib
import json
from pathlib import Path, PurePosixPath

def balanced_members(owners, plan_path):
    plan=json.loads(Path(plan_path).read_text())
    if plan.get("schema")!="minic-gnu-seven-balanced-v1" or plan.get("reference_run")!=39033023656:
        raise ValueError("GNU balanced plan provenance mismatch")
    if len(owners)!=2064 or len(set(owners))!=2064:
        raise ValueError("GNU balanced owner count or duplicates")
    if any(PurePosixPath(name).is_absolute() or ".." in PurePosixPath(name).parts
           or not name.endswith(".o") for name in owners):
        raise ValueError("unsafe GNU balanced owner")
    names=sorted(owners)
    source_hash=hashlib.sha256(("\n".join(names)+"\n").encode()).hexdigest()
    if source_hash!=plan.get("sorted_owner_sha256"):
        raise ValueError("GNU balanced owner universe SHA mismatch")
    digits=plan.get("digits_by_sorted_owner")
    if not isinstance(digits,str) or len(digits)!=2064 or any(c not in "0123456" for c in digits):
        raise ValueError("invalid GNU balanced assignment")
    if hashlib.sha256(digits.encode()).hexdigest()!=plan.get("assignment_sha256"):
        raise ValueError("GNU balanced assignment SHA mismatch")
    assignments=dict(zip(names,map(int,digits)))
    groups=[[name for name in owners if assignments[name]==shard] for shard in range(7)]
    if [len(g) for g in groups]!=[295,295,295,295,295,295,294]:
        raise ValueError("GNU balanced unexpected shard capacities")
    if plan.get("observed_reference_stage_totals_ms",{}).get("minic")!=1175746:
        raise ValueError("GNU balanced reference runtime data mismatch")
    return groups

def main():
    import argparse
    p=argparse.ArgumentParser()
    p.add_argument("--plan",required=True,type=Path)
    p.add_argument("--owners",required=True,type=Path)
    a=p.parse_args()
    owners=[v.strip() for v in a.owners.read_text().splitlines() if v.strip() and not v.startswith("#")]
    groups=balanced_members(owners,a.plan)
    assert [set(x) for x in groups]==[set(x) for x in balanced_members(owners[::-1],a.plan)]
    try:
        balanced_members(owners[:-1],a.plan)
    except ValueError:
        pass
    else:
        raise AssertionError("missing owner accepted")
    print("GNU_BALANCED_PLAN=PASS 2064 owners seven groups exact SHA bound")

if __name__=="__main__":
    main()
