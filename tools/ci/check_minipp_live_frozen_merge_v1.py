#!/usr/bin/env python3
"""Verify the exact five-job MiniPP live + frozen oracle convergence."""
from pathlib import Path
import re, subprocess

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / ".github/workflows/minipp-linux-frozen-v1.yml"
SOURCES = {
 "minipp-linux-frozen-v1.yml": ("2f2d22fd0183f4fc4e399497b1b3745b89d78da8", ("exact-shard", "cached-focus")),
 "minipp-linux-exact-smoke.yml": ("02c08d5b76501be16277ef233751f7ed191c15cd", ("linux-exact-smoke", "linux-exact-batch", "linux-exact-72")),
}
MODES = {
 "exact-shard": ("exact", "[minipp-exact]"),
 "cached-focus": ("focus", "[minipp-focus]"),
 "linux-exact-smoke": ("smoke", "[minipp-linux-smoke]"),
 "linux-exact-batch": ("batch", "[minipp-linux-batch]"),
 "linux-exact-72": ("72", "[minipp-linux-72]"),
}
def jobs(text):
 section = text.split("\njobs:\n",1)[1]
 marks = list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$",section))
 return {m.group(1):section[m.start():marks[i+1].start() if i+1<len(marks) else len(section)].strip() for i,m in enumerate(marks)}
def executable(text):
 return "\n".join(line for line in text.splitlines()
     if not re.search(r"^    if: |^    concurrency:$|^      group: minipp-linux-exact-|^      cancel-in-progress: true$",line)).strip()
def main():
 current = WORK.read_text()
 j = jobs(current)
 if set(j) != set(MODES): raise AssertionError(f"independent MiniPP job identities drifted: {sorted(j)}")
 for file,(sha,ids) in SOURCES.items():
  path = ROOT/".github/workflows-disabled"/file
  h = subprocess.check_output(["git","hash-object",str(path)],text=True).strip()
  if h != sha: raise AssertionError(f"archived original SHA differs: {file}")
  original=jobs(path.read_text())
  if set(original) != set(ids): raise AssertionError(f"source job set changed: {file}")
  for name in ids:
   if executable(original[name])!=executable(j[name]): raise AssertionError(f"executable oracle changed: {name}")
 for name,(mode,tag) in MODES.items():
  job = j[name]
  if ("inputs.mode == '"+mode+"'") not in job or "inputs.mode == 'all'" not in job:
   raise AssertionError(f"manual MiniPP cohort mode lost: {name}")
  if ("contains(github.event.head_commit.message, '"+tag+"')") not in job:
   raise AssertionError(f"commit message opt-in tag lost: {name}")
 if "      - 'toolchain/minipp-*'" not in current:
  raise AssertionError("Live-only legacy branch glob missing")
 for name in ("exact-shard","cached-focus"):
  for branch in ("agent/linux-expanded-kbuild-v0","agent/linux-perf-boolean-domain-v1"):
   if "refs/heads/"+branch not in j[name]:
    raise AssertionError(f"Frozen-only branch gate lost: {name}")
 for name in ("linux-exact-smoke","linux-exact-batch","linux-exact-72"):
  want = "group: minipp-linux-exact-" + "$" + "{{ github.ref }}-" + name
  if want not in j[name] or "cancel-in-progress: true" not in j[name]:
   raise AssertionError(f"independent Live-Kbuild concurrency lane lost: {name}")
 if "default: exact" not in current or "MINIPP_LINUX_ARCHIVE_SHA256: dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932" not in current:
  raise AssertionError("Frozen corpus checksum or canonical manual default changed")
 print("M0_MINIPP_LIVE_FROZEN=PASS archived_original_workflows=2 independent_jobs=5 unchanged_executable_bodies=5")
if __name__=="__main__": main()
