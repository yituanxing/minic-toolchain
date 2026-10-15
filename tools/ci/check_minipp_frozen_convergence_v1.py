#!/usr/bin/env python3
"""Assert byte-preserved MiniPP source archives and exact frozen job bodies."""
from pathlib import Path
import os, re, subprocess

ROOT = Path(__file__).resolve().parents[2]
CANONICAL = ROOT / ".github/workflows/minipp-linux-frozen-v1.yml"
SPECS = {
 "exact-shard": ("minipp-linux-exact-v1.yml", "exact", "[minipp-exact]", "minipp-linux-exact-v1", "false",
                 "92e53a34032177800251d00ff0ca2fd142ffdd15", "ee164e55bbeae0e1377f307ae70314dd4030143f"),
 "cached-focus": ("minipp-linux-focus-v1.yml", "focus", "[minipp-focus]", "minipp-linux-focus-v1", "true",
                  "08ca506dc4d6933f81648d7156cf3f698fc0a17d", "796e5b21a380aa9a2d9426ca0ecf1d12b49cf5fe"),
}
def jobs(data):
 body = data.split("\njobs:\n", 1)[1]
 ms = list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$", body))
 return {m.group(1): body[m.start():ms[i+1].start() if i+1<len(ms) else len(body)].strip() for i,m in enumerate(ms)}
def clean(text):
 return "\n".join(s for s in text.splitlines() if not re.search(
  r"^    if: |^    concurrency:$|^      group: minipp-linux-|^      cancel-in-progress: ",s)).strip()
def main():
 branch=os.environ.get("GITHUB_REF_NAME","agent/linux-expanded-kbuild-v0")
 if branch not in ("agent/linux-expanded-kbuild-v0","agent/linux-perf-boolean-domain-v1"):
  raise AssertionError(f"unsupported branch {branch}")
 current=jobs(CANONICAL.read_text())
 if set(current)!=set(SPECS)|{'linux-exact-smoke','linux-exact-batch','linux-exact-72'}: raise AssertionError('canonical frozen/live job ids changed')
 for name,(filename,mode,tag,group,cancel,rt_sha,pf_sha) in SPECS.items():
  path=ROOT/".github/workflows-disabled"/filename
  actual=subprocess.check_output(["git","hash-object",str(path)],text=True).strip()
  expect=rt_sha if branch=="agent/linux-expanded-kbuild-v0" else pf_sha
  if actual!=expect: raise AssertionError(f"archived blob sha drift for {filename}")
  old=jobs(path.read_text())
  if set(old)!={name} or clean(old[name])!=clean(current[name]):
   raise AssertionError(f"job body changed: {name}")
  now=current[name]
  if f"inputs.mode == '{mode}'" not in now or "inputs.mode == 'all'" not in now:
   raise AssertionError(f"missing manual mode {name}")
  if f"contains(github.event.head_commit.message, '{tag}')" not in now:
   raise AssertionError(f"missing push tag {name}")
  if f"group: {group}-" not in now or f"cancel-in-progress: {cancel}" not in now or "matrix.id" not in now:
   raise AssertionError(f"lost matrix-aware concurrency {name}")
 print(f"M0_MINIPP_FROZEN=PASS branch={branch} original_frozen_jobs=2 canonical_total_jobs=5 archived_blobs=2")
if __name__=="__main__": main()
