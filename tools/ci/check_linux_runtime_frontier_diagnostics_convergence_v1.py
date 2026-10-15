#!/usr/bin/env python3
"""Verify canonical Runtime full, fast and QEMU frontier job bodies and exact archives."""
from pathlib import Path
import re, subprocess

ROOT=Path(__file__).resolve().parents[2]
CANONICAL=ROOT/".github/workflows/linux-runtime-focused-faults-v1.yml"
SPECS={
 "candidate":("linux-runtime-frontier-v1.yml","ba568cb98cd92feabf75d45489d74ba003d83baa","full","[linux-runtime-frontier-v1]","linux-runtime-frontier-v1"),
 "frontier-fast-v5":("linux-runtime-frontier-fast-v5.yml","457a53c1cdb01b022a1566c7fe9f0d4b4e438d7b","fast","[linux-runtime-frontier-fast-v5]","linux-runtime-frontier-fast-v5"),
 "qemu-runtime":("linux-image-qemu-runtime-v0.yml","d537267fc422bb4c14f143e2311e39a1fb9d46c5","qemu","[linux-image-qemu-runtime-v0]","linux-image-qemu-runtime-v0"),
}
def jobs(data):
 body=data.split("\njobs:\n",1)[1]
 ms=list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$",body))
 return {m.group(1):body[m.start():ms[i+1].start() if i+1<len(ms) else len(body)].strip() for i,m in enumerate(ms)}
def clean(job):
 return "\n".join(x for x in job.splitlines() if not re.search(
  r"^    if: |^    concurrency:$|^      group: linux-|^      cancel-in-progress: ",x)).strip()
def main():
 text=CANONICAL.read_text()
 merged=jobs(text)
 if set(merged)!=set(SPECS)|{'cpu-stall','fork-stack','fault-context','satp-refresh'}: raise AssertionError(f"combined focused/frontier job set differs: {sorted(merged)}")
 for name,(filename,sha,mode,tag,group) in SPECS.items():
  path=ROOT/".github/workflows-disabled"/filename
  actual=subprocess.check_output(["git","hash-object",str(path)],text=True).strip()
  if actual!=sha: raise AssertionError(f"source archive SHA mismatch {filename}")
  old=jobs(path.read_text())
  if set(old)!={name} or clean(old[name])!=clean(merged[name]):
   raise AssertionError(f"executable job changed {name}")
  c=merged[name]
  if f"inputs.mode == '{mode}'" not in c or "inputs.mode == 'all'" not in c:
   raise AssertionError(f"manual selection changed {name}")
  if f"contains(github.event.head_commit.message, '{tag}')" not in c:
   raise AssertionError(f"push tag removed {name}")
  if f"group: {group}-" not in c or "cancel-in-progress: true" not in c:
   raise AssertionError(f"concurrency scope changed {name}")
 print("M0_RUNTIME_FRONTIER_DIAGNOSTICS=PASS jobs=3 archived_blobs=3 identical_job_bodies=3")
if __name__=="__main__":main()
