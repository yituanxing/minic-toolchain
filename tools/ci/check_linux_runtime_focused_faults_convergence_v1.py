#!/usr/bin/env python3
"""Check original Runtime diagnosis body, tags, archived SHA and concurrency."""
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
CANONICAL=ROOT/".github/workflows/linux-runtime-focused-faults-v1.yml"
SPECS={
 "cpu-stall":("linux-check-cpu-stall-runtime-v0.yml","7fce9bc454cec54411fb852137fa627863ef8022","runtime","cpu-stall","[linux-check-cpu-stall-runtime-v0]","linux-check-cpu-stall-runtime-v0",None),
 "fork-stack":("linux-fork-stack-runtime-v0.yml","db96f28725b15954d39dad975598b99a0497a0ec","runtime","fork-stack","[linux-fork-stack-runtime-v0]","linux-fork-stack-runtime-v0","case"),
 "fault-context":("linux-runtime-fault-context-v1.yml","4ce700578911665cf0d5fd0085d511da7d7e3d27","fault-context","fault-context","[linux-runtime-fault-context-v1]","linux-runtime-fault-context-v1",None),
 "satp-refresh":("linux-runtime-satp-refresh-v0.yml","defbf066e5bb04d2ddd56f3695085bc00b2d3f08","satp-refresh","satp","[linux-runtime-satp-refresh-v0]","linux-runtime-satp-refresh-v0",None),
}
def jobs(data):
 section=data.split("\njobs:\n",1)[1]
 ms=list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$",section))
 return {m.group(1):section[m.start():ms[i+1].start() if i+1<len(ms) else len(section)].strip() for i,m in enumerate(ms)}
def normalized(job):
 return re.sub(r"^[a-z][a-z0-9-]*:", "JOB:", "\n".join(
   line for line in job.splitlines()
   if not re.search(r"^    if: |^    concurrency:$|^      group: linux-|^      cancel-in-progress: ",line)
 )).strip()
def main():
 text=CANONICAL.read_text()
 current=jobs(text)
 if set(current)!=set(SPECS): raise AssertionError(f"job set changed: {sorted(current)}")
 if '      - "agent/linux-expanded-kbuild-v0"' not in text:
  raise AssertionError("original Runtime push owner not retained")
 for name,(filename,sha,oldname,mode,tag,group,matrix) in SPECS.items():
  archive=ROOT/".github/workflows-disabled"/filename
  actual=subprocess.check_output(["git","hash-object",str(archive)],text=True).strip()
  if actual!=sha: raise AssertionError(f"archive corruption {filename}")
  original=jobs(archive.read_text())
  if set(original)!={oldname}: raise AssertionError(f"original job set changed: {filename}")
  if normalized(current[name])!=normalized(original[oldname]):
   raise AssertionError(f"job executable body changed {name}")
  currentjob=current[name]
  if f"inputs.mode == '{mode}'" not in currentjob or "inputs.mode == 'all'" not in currentjob:
   raise AssertionError(f"dispatch mismatch {name}")
  if f"contains(github.event.head_commit.message, '{tag}')" not in currentjob:
   raise AssertionError(f"original tag missing {name}")
  if f"group: {group}-" not in currentjob or "cancel-in-progress: true" not in currentjob:
   raise AssertionError(f"concurrency identity lost {name}")
  if matrix and f"matrix.{matrix}" not in currentjob:
   raise AssertionError(f"matrix shard cancellation hazard: {name}")
 print("M0_RUNTIME_FOCUSED_FAULTS=PASS source_blobs=4 executable_jobs=4 original_tags=4")
if __name__=="__main__":main()
