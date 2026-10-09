#!/usr/bin/env python3
"""Assert Linux Core eight-job union preserves both original canonical YAMLs.

All eight original job strings must match byte-for-byte, including each of the
distinct 3352 and GNU assembler aggregation/provenance/opt-in contracts.
"""
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
CURRENT=ROOT/".github/workflows/linux-core-all3352.yml"
PREVIOUS={
    "linux-core-all3352.yml":(
        "c484a871807795b15c2f43de8acdaf1071fcba57",
        {"first500","shards","all3352","asm-first500","asm-shards","asm-all3352"}),
    "core-first500-regression.yml":(
        "02627bc5f2a3dc50dbca227cde4c8157bef3f043",
        {"strict500","focused-five"}),
}
def jobs(content):
    tail=content.split("\njobs:\n",1)[1]
    matches=list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$",tail))
    return {
        m.group(1):tail[m.start():matches[i+1].start() if i+1<len(matches) else len(tail)].strip()
        for i,m in enumerate(matches)
    }
def main():
    now=CURRENT.read_text()
    current=jobs(now)
    expected=set().union(*(jobs for _,jobs in PREVIOUS.values()))
    if set(current)!=expected:
        raise AssertionError(f"Core job set drift: {set(current)^expected}")
    for filename,(sha,identifiers) in PREVIOUS.items():
        archive=ROOT/".github/workflows-disabled"/filename
        actual=subprocess.check_output(["git","hash-object",str(archive)],text=True).strip()
        if sha!=actual:
            raise AssertionError(f"canonical Git source SHA lost: {filename}")
        old=jobs(archive.read_text())
        if set(old)!=identifiers:
            raise AssertionError(f"original Core jobs changed: {filename}")
        for name in identifiers:
            if old[name]!=current[name]:
                raise AssertionError(f"executable Core job body changed: {name}")
    for mode in ("compile","assemble","strict500","focused-five","all"):
        if "          - "+mode not in now:
            raise AssertionError(f"Core manual dispatch mode missing: {mode}")
    for tag in ("[all3352]","[linux-core-all3352]","[linux-assemble-all3352]","[regress500]","[linux-five]"):
        if tag not in now:
            raise AssertionError(f"original Core heavy opt-in tag missing: {tag}")
    for b in ("agent/linux-expanded-kbuild-v0","agent/linux-perf-boolean-domain-v1"):
        if b not in now:
            raise AssertionError(f"current development branch push lost: {b}")
    for path in ("'src/**'","'include/**'","'tests/external/linux/**'","'tools/ci/**'","'Makefile'"):
        if path not in now:
            raise AssertionError(f"original Core relevant path trigger missing: {path}")
    print("M0_CORE_3352_FIRST500_UNION=PASS archived_canons=2 unchanged_original_jobs=8 distinct_frozen_and_assembler_oracles=8")
if __name__=="__main__": main()
