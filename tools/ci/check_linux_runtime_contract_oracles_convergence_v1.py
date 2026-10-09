#!/usr/bin/env python3
"""Static regression for four original compiler semantics / QEMU watcher jobs."""
from pathlib import Path
import re, subprocess
ROOT=Path(__file__).resolve().parents[2]
TARGET=ROOT/".github/workflows/linux-expanded-pi-p1-runtime-v1.yml"
ARCHIVES={
 "linux-runtime-compiler-semantics-v1.yml":("8601132b3f798dbfb0d57d3c3bb8eb802ea99c1b","semantics",("pi-local-symbol","satp-micro"),"linux-runtime-compiler-semantics-v1"),
 "linux-runtime-qemu-watch-contracts-v1.yml":("c7f1311975ffd5703d216d1973ff6aa206ca6370","watch",("qemu-watch-cert","inconclusive-cert"),"linux-runtime-qemu-watch-contracts-v1"),
}
def jobs(content):
 body=content.split("\njobs:\n",1)[1]
 marks=list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$",body))
 return {m.group(1):body[m.start():marks[i+1].start() if i+1<len(marks) else len(body)].strip() for i,m in enumerate(marks)}
def normal(s):
 return "\n".join(x for x in s.splitlines() if not re.search(
  r"^    if: |^    concurrency:$|^      group: linux-runtime-(?:compiler-semantics-v1|qemu-watch-contracts-v1)-|^      cancel-in-progress: true$",x)).strip()
def main():
 s=TARGET.read_text()
 current=jobs(s)
 if set(current)!={"pi-runtime","p1","trace","pi-local-symbol","satp-micro","qemu-watch-cert","inconclusive-cert"}:
  raise AssertionError("independent jobs lost")
 for filename,(sha,mode,ids,group) in ARCHIVES.items():
  p=ROOT/".github/workflows-disabled"/filename
  h=subprocess.check_output(["git","hash-object",str(p)],text=True).strip()
  if h!=sha:raise AssertionError("archived original SHA changed: "+filename)
  old=jobs(p.read_text())
  if set(old)!=set(ids):raise AssertionError("source job set differs")
  for n in ids:
   if normal(old[n])!=normal(current[n]):raise AssertionError("executable body changed: "+n)
   v=current[n]
   if "inputs.mode == '"+mode+"'" not in v or "inputs.mode == 'all'" not in v:raise AssertionError("selection changed: "+n)
   if "github.event_name == 'push'" not in v or "github.event_name == 'workflow_dispatch'" not in v:raise AssertionError("push/dispatch lost: "+n)
   if "group: "+group+"-" not in v or "github.ref }}-"+n not in v or "cancel-in-progress: true" not in v:
    raise AssertionError("per-job concurrency was lost: "+n)
 for tag in ("[linux-runtime-pi-local-symbol-v0]","[linux-pi-micro]","[runtime-compiler-semantics-v1]","[linux-runtime-qemu-watch-cert-v1]","[linux-runtime-qemu-watch-inconclusive-v1]","[linux-runtime-qemu-watch-contracts-v1]"):
  if tag not in s:raise AssertionError("old tag missing: "+tag)
 print("M0_RUNTIME_CONTRACT_ORACLES=PASS archived=2 jobs=4 exact_execution_body=4")
if __name__=="__main__":main()
