#!/usr/bin/env python3
"""Exact Git blob and job-body proof for expanded PI/P1 Runtime consolidation."""
from pathlib import Path
import os, re, subprocess

ROOT=Path(__file__).resolve().parents[2]
PERF = "agent/linux-perf-boolean-domain-v1"
PERF_PI_ARCHIVE = ROOT / ".github/workflows-disabled/performance-retired-2026-10-09/linux-expanded-pi-p1-runtime-v1.yml"
CANONICAL = (PERF_PI_ARCHIVE if os.environ.get("GITHUB_REF_NAME") == PERF else
           ROOT / ".github/workflows/linux-expanded-pi-p1-runtime-v1.yml")
TRACE_BLOBS={
 "agent/linux-expanded-kbuild-v0":"7b40efe922e288bb6dadab208f851849867d03b0",
 "agent/linux-perf-boolean-domain-v1":"ff15c0643a93b8025fb32766ad94bb46123ae67e",
}
REF=os.environ.get("GITHUB_REF_NAME","agent/linux-expanded-kbuild-v0")
if REF not in TRACE_BLOBS:raise AssertionError(f"Unrecognized branch for historical Entry Trace: {REF}")
SPECS={
 "pi-runtime":("linux-expanded-pi-runtime-v0.yml","40b22c2ff0bda380df99ebbab99cea55f424aaa8","pi","[linux-expanded-pi-runtime]"),
 "p1":("linux-expanded-runtime-p1-v0.yml","93677aa0c74dc00cfba0544535fca5b841dd2b51","p1","[linux-expanded-runtime-p1]"),
 "trace":("linux-expanded-entry-trace-v0.yml",TRACE_BLOBS[REF],"trace","[linux-expanded-entry-trace]"),
}
def jobs(text):
 b=text.split("\njobs:\n",1)[1]
 m=list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$",b))
 return {v.group(1):b[v.start():m[i+1].start() if i+1<len(m) else len(b)].strip() for i,v in enumerate(m)}
def normalized(s):
 return "\n".join(l for l in s.splitlines() if not re.search(
  r"^    if: |^    concurrency:$|^      group: (?:linux-expanded-pi-runtime-v0|linux-expanded-entry-trace)-|^      cancel-in-progress: true$", l)).strip()
def main():
 s=CANONICAL.read_text()
 merged=jobs(s)
 if set(merged)!=set(SPECS)|{"pi-local-symbol","satp-micro","qemu-watch-cert","inconclusive-cert"}: raise AssertionError(f"merged early-runtime jobs changed: {merged.keys()}")
 for name,(file,sha,mode,tag) in SPECS.items():
  path=ROOT/".github/workflows-disabled"/file
  origSha=subprocess.check_output(["git","hash-object",str(path)],text=True).strip()
  if sha!=origSha:raise AssertionError(f"historical blob changed: {file}")
  originals=jobs(path.read_text())
  if set(originals)!={name} or normalized(originals[name])!=normalized(merged[name]):
   raise AssertionError(f"executable job body altered: {name}")
  candidate=merged[name]
  if f"inputs.mode == '{mode}'" not in candidate or "inputs.mode == 'all'" not in candidate:
   raise AssertionError(f"manual dispatch mode removed: {name}")
  if f"contains(github.event.head_commit.message, '{tag}')" not in candidate:
   raise AssertionError(f"legacy push tag altered: {name}")
 if "group: linux-expanded-pi-runtime-v0-" not in merged["pi-runtime"]:
  raise AssertionError("PI concurrency group changed")
 if "cancel-in-progress: true" not in merged["pi-runtime"]:
  raise AssertionError("PI cancel policy changed")
 if "    concurrency:" in merged["p1"]:
  raise AssertionError("P1 acquired unreviewed concurrency")
 if "group: linux-expanded-entry-trace-" not in merged["trace"] or "cancel-in-progress: true" not in merged["trace"]:
  raise AssertionError("Trace concurrency changed")
 if "          - trace" not in s or "inputs.mode == 'trace'" not in s:
  raise AssertionError("Trace manual dispatch mode absent")
 print(f"M0_LINUX_EXPANDED_PI_P1_TRACE=PASS branch={REF} archived_blobs=3 job_bodies=3 tags=3")
if __name__=="__main__":main()
