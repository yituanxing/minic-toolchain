#!/usr/bin/env python3
"""M0: prove true single-build T1 keeps all original RV64/AR/LD test oracles.

Original three independent workflow snapshots and prior three-job canonical
are byte-pinned. Exact executable test step bodies must only differ in the
shared BUILD_DIR and additional mode/outcome guards. Independent failures must
fail the final job after testing all selected owners.
"""
from pathlib import Path
import ast, hashlib, os, re, fnmatch

ROOT=Path(__file__).resolve().parents[2]
BRANCHES=("agent/linux-expanded-kbuild-v0","agent/linux-perf-boolean-domain-v1")
OLD=["minic-rv64-focused-regressions-v1.yml","miniar-regressions-v1.yml","minild-regressions-v1.yml"]
OLD_SHA=[
("883f7d7cfea0dc1f66154955783e969e3391061d","ac16bda61647a955353c2d580725273c6955c6fb","3b05489a952152f810c70afa6b43fa252ddf47d5"),
("b4df1d99eb1481f0df2d4e941a2d3024ef28815c","31844b55896702273ebc5efd08518d2de7f536d6","ec6e534cc4020442bdb58dcb64fc338276bd8641")]
THREE_SHA="2b5e3809fe9fb8afc31aad988bd11333c8122901"
THREE_ARCHIVE=ROOT/".github/workflows-disabled/toolchain-focused-regressions-three-jobs-2026-10-09.yml"
CURRENT=ROOT/".github/workflows/toolchain-focused-regressions-v1.yml"
BUILD_DIR="build/toolchain-focused-t1-v2"
OWNERS=(
 ("minic-rv64","rv64","build/rv64-focused-v1-toolchain",(
   ("Run focused current-product regressions","rv64"),)),
 ("miniar","miniar","build/miniar-regressions-v1",(
   ("Validate MiniAR archive contracts","ar_archive"),
   ("Validate shared ELF reader contract","ar_reader"),
   ("Validate MiniNM archive consumers","ar_nm"))),
 ("minild","minild","build/minild-regressions-v1",(
   ("Validate shared ELF writer reader and linker-script contracts","ld_shared"),
   ("Validate MiniLD static A0-A3 contracts","ld_static"),
   ("Validate MiniLD dynamic A4-A6 contracts","ld_dynamic"),
   ("Validate MiniLD section GC contract","ld_gc")))
)

def sha(data):
 return hashlib.sha1(b"blob "+str(len(data)).encode()+b"\0"+data).hexdigest()

def step(txt,name):
 token="      - name: "+name+"\n"
 if txt.count(token)!=1: raise AssertionError("step absent/duplicate "+name)
 tail=txt.split(token,1)[1]
 end=tail.find("\n      - name:")
 if end>=0: tail=tail[:end]
 return (token+tail).rstrip("\n")

def accepted(globs,path):
 match=False
 for pattern in globs:
  neg=pattern.startswith("!")
  if fnmatch.fnmatchcase(path,pattern[1:] if neg else pattern):
   match=not neg
 return match

def main():
 branch=os.environ.get("GITHUB_REF_NAME",BRANCHES[0])
 if branch not in BRANCHES: raise AssertionError("unknown branch")
 txt=CURRENT.read_text()
 h,jobs=txt.split("\njobs:\n",1)
 original=THREE_ARCHIVE.read_bytes()
 if sha(original)!=THREE_SHA: raise AssertionError("original 3-job canonical altered")
 assert set(re.findall(r"(?m)^  ([a-z][\w-]*):\s*$",jobs))=={"focused-t1"}
 for name,hashes in zip(OLD,zip(*OLD_SHA)):
  if (ROOT/".github/workflows"/name).exists():raise AssertionError("old separate YAML returned")
  blob=(ROOT/".github/workflows-disabled"/name).read_bytes()
  if sha(blob)!=hashes[BRANCHES.index(branch)]:
   raise AssertionError("original historical regression workflow changed "+name)
 g=re.search(r"(?m)^    paths:\n((?:      - [^\n]*\n)+)",h)
 if g is None:raise AssertionError("source triggers absent")
 paths=[ast.literal_eval(x.strip()[2:].strip()) for x in g.group(1).splitlines()]
 if paths[-1]!="!tools/ci/**":raise AssertionError("CI-helper exclusion lost")
 for source in ("src/frontend/parse.c","archiver/miniar.c","linker/minild.c","elf/src/reader.c","tools/minic-cc/driver.c","Makefile"):
  if not accepted(paths,source):raise AssertionError("real source no longer triggers T1 "+source)
 for bad in ("tools/ci/runtime-init-irq-trigger.txt","tools/ci/check_ci_trigger_coverage_v1.py","tools/ci/apply-perf-core-object-interval-onepass-v1.py"):
  if accepted(paths,bad):raise AssertionError("non-source CI helper triggers T1 "+bad)
 for branch in BRANCHES:
  if '"'+branch+'"' not in h:raise AssertionError("lost T1 branch "+branch)
 for mode in ("all","minic-rv64","miniar","minild"):
  if "          - "+mode not in h:raise AssertionError("manual mode lost "+mode)
 if txt.count("make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="+BUILD_DIR+" all")!=1:
  raise AssertionError("shared toolchain must be built exactly once")
 if "libc6-dev-riscv64-cross qemu-user" not in jobs or "binutils" not in jobs:
  raise AssertionError("required union of linker/assembler/emulator packages lost")
 if "timeout-minutes: 50" not in jobs: raise AssertionError("shared runner timeout changed without review")
 archived=original.decode()
 norm_steps=[]
 for oldjob,mode,old_dir,tests in OWNERS:
  older=archived.split("\n  "+oldjob+":\n",1)
  if len(older)!=2:raise AssertionError("historic owner missing")
  oldbody=re.split(r"\n  [a-z][\w-]*:\n",older[1],maxsplit=1)[0]
  for name,step_id in tests:
   former=step(oldbody,name)
   now=step(jobs,name)
   prefix=(
     "        id: "+step_id+"\n"
     "        if: (github.event_name == 'push' || inputs.mode == 'all' || inputs.mode == '"+mode+"') && steps.build.outcome == 'success'\n"
     "        continue-on-error: true\n"
   )
   if now.count(prefix)!=1:raise AssertionError("owner gating / continue missing "+step_id)
   reconstructed=now.replace(prefix,"").replace(BUILD_DIR,old_dir)
   if reconstructed!=former:
    raise AssertionError("original independent T1 test script altered "+step_id)
   norm_steps.append(step_id)
 if len(norm_steps)!=8:raise AssertionError("eight original test steps not preserved")
 for ident in norm_steps:
  if "steps."+ident+".outcome" not in jobs:
   raise AssertionError("verdict aggregation could mask failure "+ident)
 if '          exit "$failures"' not in jobs:
  raise AssertionError("independent T1 outcomes must fail overall job")
 for important in ("TOOLCHAIN_FOCUSED_T1=PASS","TOOLCHAIN_FOCUSED_T1=FAIL",
                   "minic-rv64-focused-regressions-v1",
                   "toolchain-focused-t1-suite-verdict"):
  if important not in jobs:raise AssertionError("lost regression evidence "+important)
 print(f"M0_FOCUSED_T1_SINGLE_BUILD=PASS branch={branch} historical_three_workflows=3 pinned_three_job_snapshot=1 exact_independent_test_steps=8 build_all=1 selected_failure_aggregation=PASS")

if __name__=="__main__":main()
