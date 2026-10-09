#!/usr/bin/env python3
"""M0 exact-job and routing audit of consolidated RV64, MiniAR, MiniLD T1 suites."""
from pathlib import Path
import hashlib,os,re,fnmatch,ast

ROOT=Path(__file__).resolve().parents[2]
BRANCHES=("agent/linux-expanded-kbuild-v0","agent/linux-perf-boolean-domain-v1")
OLD=["minic-rv64-focused-regressions-v1.yml","miniar-regressions-v1.yml","minild-regressions-v1.yml"]
JOBS=["minic-rv64","miniar","minild"]
PINNED=[["883f7d7cfea0dc1f66154955783e969e3391061d","ac16bda61647a955353c2d580725273c6955c6fb","3b05489a952152f810c70afa6b43fa252ddf47d5"],["b4df1d99eb1481f0df2d4e941a2d3024ef28815c","31844b55896702273ebc5efd08518d2de7f536d6","ec6e534cc4020442bdb58dcb64fc338276bd8641"]]
NEW=ROOT/".github/workflows/toolchain-focused-regressions-v1.yml"

def gitsha(data):
 return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()

def included(patterns,path):
 ok=False
 for pat in patterns:
  negative=pat.startswith("!")
  if fnmatch.fnmatchcase(path,pat[1:] if negative else pat): ok=not negative
 return ok

def main():
 branch=os.environ.get("GITHUB_REF_NAME",BRANCHES[0])
 if branch not in BRANCHES: raise AssertionError("unsupported ref")
 text=NEW.read_text()
 assert text.count("\njobs:\n")==1
 h, jobs=text.split("\njobs:\n",1)
 assert "  workflow_dispatch:" in h
 for b in BRANCHES:
  assert '      - "'+b+'"' in h
 paths=re.search(r"(?m)^    paths:\n((?:      - [^\n]*\n)+)",h)
 if paths is None: raise AssertionError("missing positive/negative paths")
 globs=[ast.literal_eval(s.strip()[2:].strip()) for s in paths.group(1).splitlines()]
 for src in ("elf/src/reader.c","elf/src/rewrite.c","src/frontend/parse.c",
             "archiver/miniar.c","linker/minild.c","tests/compiler/c0/check.c",
             ".github/workflows/toolchain-focused-regressions-v1.yml"):
  if not included(globs,src): raise AssertionError("missing positive trigger: "+src)
 for src in ("tools/ci/runtime-timekeeping-trigger.txt",
             "tools/ci/select_runtime_owner_modes_v1.py",
             "tools/ci/check_ci_trigger_coverage_v1.py",
             "tools/ci/select_miniobjcopy_elf_route_v1.py"):
  if included(globs,src): raise AssertionError("wasted T1 trigger: "+src)
 for i,(name,job) in enumerate(zip(OLD,JOBS)):
  if (ROOT/".github/workflows"/name).exists():
   raise AssertionError("old T1 workflow still active: "+name)
  raw=(ROOT/".github/workflows-disabled"/name).read_bytes()
  if gitsha(raw)!=PINNED[BRANCHES.index(branch)][i]:
   raise AssertionError("archived historical Git blob altered: "+name)
  marker="\njobs:\n  regressions:\n"
  if raw.decode().count(marker)!=1: raise AssertionError("historical job changed")
  original=raw.decode().split(marker)[1].rstrip("\n")
  pattern=r"(?ms)^  "+re.escape(job)+r":\n(.*?)(?=^  [a-z][a-z0-9-]*:\n|\Z)"
  match=re.search(pattern,jobs)
  if match is None: raise AssertionError("new canonical owner absent: "+job)
  body=match.group(1)
  required="    if: github.event_name == 'push' || (github.event_name == 'workflow_dispatch' && (inputs.mode == 'all' || inputs.mode == '"+job+"'))\n"
  if not body.startswith(required):
   raise AssertionError("manual/push mode guard changed: "+job)
  if body[len(required):].rstrip("\n")!=original:
   raise AssertionError("original full build/test/upload job body modified: "+job)
 if len(re.findall(r"(?m)^  [a-z][a-z0-9-]*:\s*$",jobs))!=3:
  raise AssertionError("unexpected new canonical job count")
 print(f"M0_FOCUSED_T1=PASS branch={branch} original_full_job_bodies=3 archives_sha_exact=3 independent_jobs=3 source_routes=7 negative_routes=4")

if __name__=="__main__":main()
