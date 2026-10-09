#!/usr/bin/env python3
"""Protect the two Runtime diagnostic canons unified into seven jobs."""
from pathlib import Path
import re, subprocess

ROOT=Path(__file__).resolve().parents[2]
CURRENT=ROOT/".github/workflows/linux-runtime-focused-faults-v1.yml"
SOURCES={
 "linux-runtime-focused-faults-v1.yml":("770c2bb3c8831b35d7eabebbc8dba8d09dc32384",("cpu-stall","fork-stack","fault-context","satp-refresh")),
 "linux-runtime-frontier-diagnostics-v1.yml":("2e6b1ff2a4b6f668432a0329e01b01376a20d8d7",("candidate","frontier-fast-v5","qemu-runtime")),
}
MODES=("fault-context","cpu-stall","fork-stack","satp","fast","full","qemu","all")
def jobs(text):
 s=text.split("\njobs:\n",1)[1]
 m=list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$",s))
 return {x.group(1):s[x.start():m[i+1].start() if i+1<len(m) else len(s)].strip()
         for i,x in enumerate(m)}
def main():
 combined=CURRENT.read_text()
 now=jobs(combined)
 if len(now)!=7:raise AssertionError("seven distinct diagnostic jobs required")
 for filename,(sha,expected) in SOURCES.items():
  archived=ROOT/".github/workflows-disabled"/filename
  actual=subprocess.check_output(["git","hash-object",str(archived)],text=True).strip()
  if actual!=sha:raise AssertionError(f"archived source changed: {filename}")
  previous=jobs(archived.read_text())
  if set(previous)!=set(expected):raise AssertionError(f"original job set changed: {filename}")
  for name in expected:
   if previous[name]!=now.get(name):
    raise AssertionError(f"diagnostic executable job/configuration changed: {name}")
 if set(now)!=set(SOURCES["linux-runtime-focused-faults-v1.yml"][1]+SOURCES["linux-runtime-frontier-diagnostics-v1.yml"][1]):
  raise AssertionError("diagnostic job identities differ")
 for mode in MODES:
  if ("          - "+mode) not in combined:
   raise AssertionError(f"manual diagnostic mode absent: {mode}")
 if "default: fault-context" not in combined or '- "agent/linux-expanded-kbuild-v0"' not in combined:
  raise AssertionError("push scope/manual default changed")
 print("M0_RUNTIME_DIAGNOSTIC_UNION=PASS archived_canons=2 all_7_jobs_exact=1 manual_modes=8")
if __name__=="__main__":main()
