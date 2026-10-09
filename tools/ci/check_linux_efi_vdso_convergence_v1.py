#!/usr/bin/env python3
"""Assert EFI/vDSO job bodies, original Git blobs, tags and independent modes."""
from pathlib import Path
import re, subprocess
ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/".github/workflows/linux-efi-vdso-focused-v1.yml"
OWNERS={
 "efistub-diff":("linux-efistub-diff-v0.yml","ee010b82654c79ad1d9d8da0fb8fa3848ad75fd5","efi","[linux-efistub-diff]"),
 "vdso":("linux-vdso-focused.yml","2b3844cd7d19d9b80381068ce7947ece25a239d4","vdso","[linux-vdso-focused]"),
}
def jobs(source):
 segment=source.split("\njobs:\n",1)[1]
 matches=list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$",segment))
 return {m.group(1):segment[m.start():matches[i+1].start() if i+1<len(matches) else len(segment)].strip() for i,m in enumerate(matches)}
def normal(job):
 return "\n".join(x for x in job.splitlines() if not re.search(r"^    if: |^    concurrency:$|^      group: linux-efistub-diff-v0-|^      cancel-in-progress: true$",x)).strip()
def main():
 combined=WORK.read_text()
 new=jobs(combined)
 if set(new)!=set(OWNERS): raise AssertionError(f"changed job set: {new.keys()}")
 for name,(file,sha,mode,tag) in OWNERS.items():
  archive=ROOT/".github/workflows-disabled"/file
  h=subprocess.check_output(["git","hash-object",str(archive)],text=True).strip()
  if h!=sha: raise AssertionError(f"archive corrupted: {file}")
  original=jobs(archive.read_text())
  if set(original)!={name} or normal(original[name])!=normal(new[name]):
   raise AssertionError(f"executable job changed: {name}")
  if f"inputs.mode == '{mode}'" not in new[name] or "inputs.mode == 'all'" not in new[name]:
   raise AssertionError(f"manual mode missing: {name}")
  if f"contains(github.event.head_commit.message, '{tag}')" not in new[name]:
   raise AssertionError(f"push tag lost: {name}")
 if "    concurrency:" in new["vdso"]:
  raise AssertionError("vDSO had no cancellation behavior; do not add any")
 if "group: linux-efistub-diff-v0-" not in new["efistub-diff"] or "cancel-in-progress: true" not in new["efistub-diff"]:
  raise AssertionError("EFI cancellation scope lost")
 for p in ("'src/**'","'include/**'","'tests/external/linux/**'","'tools/ci/**'"):
  if p not in combined: raise AssertionError(f"push source scope lost: {p}")
 print("M0_EFI_VDSO=PASS jobs=2 archived_blobs=2 exact_test_bodies=2 tags=2")
if __name__=="__main__":main()
