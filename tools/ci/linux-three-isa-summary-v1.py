#!/usr/bin/env python3
"""Merge all 21 original GNU .i -> MiniC -S shard receipts by ROOT CAUSE."""
import argparse,collections,json
from pathlib import Path

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--root",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    inputs=sorted(a.root.glob("*/report.json"))
    if len(inputs)!=21:
        raise SystemExit(f"THREE_LINUX_CENSUS=INCOMPLETE shards={len(inputs)}/21")
    groups=collections.defaultdict(lambda:collections.defaultdict(list))
    totals=collections.defaultdict(lambda:dict(total=0,pass_count=0))
    seen=set()
    for f in inputs:
        j=json.loads(f.read_text())
        if j.get("verdict")!="MINIC_S_DISCOVERY_ONLY_AS_LINK_QEMU_NOT_TESTED":
            raise SystemExit("THREE_LINUX_CENSUS=FAIL dishonest verdict")
        target,shard=j["target"],j["shard"]
        key=(target,shard)
        if key in seen:raise SystemExit("THREE_LINUX_CENSUS=FAIL duplicate shard")
        seen.add(key)
        for row in j["rows"]:
            totals[target]["total"]+=1
            if row["status"]=="MINIC_S_PASS":
                totals[target]["pass_count"]+=1
            else:
                groups[row["status"]+" | "+row["diagnostic"]][target].append(row["object"])
    if seen!={(target,i) for target in ("x86_64","aarch64","loongarch64") for i in range(7)}:
        raise SystemExit("THREE_LINUX_CENSUS=FAIL missing architecture or shard")
    errors=[dict(root_cause=issue,total=sum(map(len,arch.values())),
                 targets={k:sorted(v) for k,v in arch.items()})
            for issue,arch in groups.items()]
    errors.sort(key=lambda e:-e["total"])
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(dict(
        schema="minic-linux-cross-target-error-census-v1",
        verdict="FRONTEND_AND_CODEGEN_DISCOVERY_ONLY_NOT_GNU_LINK_OR_QEMU",
        targets=totals,root_cause_groups=errors),indent=2)+"\n")
    for arch,num in sorted(totals.items()):
        print(f"THREE_LINUX_MINIC_S target={arch} compiled={num['pass_count']}/{num['total']}")
    for e in errors[:25]:
        print(f"THREE_LINUX_ROOT_CAUSE total={e['total']} targets={','.join(sorted(e['targets']))} diagnostic={e['root_cause'][:175]}")
    print(f"THREE_LINUX_CENSUS=COMPLETE shards=21 shared_root_causes={len(errors)} full_GNU_AS_link_QEMU=NOT_RUN")
if __name__=="__main__":main()
