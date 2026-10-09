#!/usr/bin/env python3
"""Ensure all four opt-in Linux performance jobs preserve their original executable assertions."""
from pathlib import Path
import re, subprocess

ROOT=Path(__file__).resolve().parents[2]
SUITE=ROOT/".github/workflows/linux-runtime-optin-perf-suite-v1.yml"
ORIGINAL={
 "linux-runtime-optin-perf-all3352-v1.yml":("81e5d4a2b55bfd68386cc844b227fbb399b60fe6",("shard","aggregate"),"all3352","[linux-perf3352]"),
 "linux-runtime-optin-perf-constant-p-qemu-v1.yml":("65c5fc802b52c171a08b2fffddc1c15802103864",("semantics",),"constant-p","[linux-constant-p]"),
 "linux-runtime-optin-perf-first500-ab-v1.yml":("b223ea9a7a728aa6e7eccf40ec76c930bcf58624",("perf500",),"first500","[linux-perf500]"),
}
def jobs(data):
 section=data.split("\njobs:\n",1)[1]
 matches=list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$",section))
 return {m.group(1):section[m.start():matches[i+1].start() if i+1<len(matches) else len(section)].strip() for i,m in enumerate(matches)}
def remove_job_if(text):
 return "\n".join(s for s in text.splitlines() if not s.startswith("    if: ")).strip()
def main():
 content=SUITE.read_text()
 current=jobs(content)
 if set(current)!={"shard","aggregate","semantics","perf500"}: raise AssertionError("changed job set")
 for filename,(sha,names,mode,tag) in ORIGINAL.items():
  archived=ROOT/".github/workflows-disabled"/filename
  obj=subprocess.check_output(["git","hash-object",str(archived)],text=True).strip()
  if obj!=sha: raise AssertionError(f"archive blob mismatch {filename}")
  originals=jobs(archived.read_text())
  if set(originals)!=set(names): raise AssertionError(f"original job identifiers changed {filename}")
  for name in names:
   if remove_job_if(originals[name])!=remove_job_if(current[name]):
    raise AssertionError(f"changed executable job body {name}")
   condition=current[name]
   if f"inputs.mode == '{mode}'" not in condition or "inputs.mode == 'all'" not in condition:
    raise AssertionError(f"manual mode lost {name}")
   if f"contains(github.event.head_commit.message, '{tag}')" not in condition:
    raise AssertionError(f"push-tag opt-in missing {name}")
 if "    needs: shard" not in current["aggregate"]:raise AssertionError("aggregate depends on shards")
 if "    if: always() && " not in current["aggregate"]:raise AssertionError("aggregate lost fail-aware reporting")
 print("M0_LINUX_PERF_SUITE=PASS jobs=4 archived_blobs=3 unchanged_job_bodies=4")
if __name__=="__main__":main()
