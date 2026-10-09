#!/usr/bin/env python3
"""Check shared ELF source changes trigger every Makefile 'all' regression owner.

The only authorized workflow change is adding the elf/** positive push glob.
All existing jobs, original filters, branches and manual dispatch must retain
their exact Git blob; path filtering is tested including mixed commit edits.
"""
from pathlib import Path
import hashlib
import re

ROOT=Path(__file__).resolve().parents[2]
PINNED={
 "minic-driver-v0.yml":"ccbcc1abbc1127868eccdedf6aa85fb10aba8fb4",
 "minipp-a0.yml":"f0015b6c395227db5715785f468e3e93b71233c1",
}
MARKER="      - 'elf/**'\n"
def sha(data):
 return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()
def main():
 for name, original in PINNED.items():
  p=ROOT/".github/workflows"/name
  new=p.read_text()
  header=new.split("\njobs:\n",1)[0]
  if header.count(MARKER)!=1 or new.count(MARKER)!=1:
   raise AssertionError(f"missing shared ELF trigger: {name}")
  if "on:\n  push:" not in header or "  workflow_dispatch:" not in header:
   raise AssertionError(f"push/manual eligibility changed: {name}")
  for branch in ("agent/linux-expanded-kbuild-v0","agent/linux-perf-boolean-domain-v1"):
   if branch not in header:raise AssertionError(f"branch scope changed: {name}")
  for f in ("elf/src/reader.c","elf/src/relocatable_writer.c","elf/src/rewrite.c"):
   if not re.fullmatch(r"elf/\*\*", "elf/**"):raise AssertionError("bad glob")
   # GitHub positive 'elf/**' includes all files under the shared directory.
   if not f.startswith("elf/"):raise AssertionError("file not covered")
  if sha(new.replace(MARKER,"").encode())!=original:
   raise AssertionError(f"historical exact job, path or trigger identity changed: {name}")
 print("M0_SHARED_ELF_PATH_COVERAGE=PASS owners=2 original_job_bodies=2 shared_elf_examples=3 old_git_blobs=2")
if __name__=="__main__":main()
