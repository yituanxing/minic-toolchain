#!/usr/bin/env python3
"""Truth-table gate for the early Runtime semantic/QEMU watcher dispatch routes.

Manual mode selection must work from either active development ref; only push
tag routing is Runtime-branch-scoped. Test steps stay byte-identical.
"""
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/".github/workflows/linux-expanded-pi-p1-runtime-v1.yml"
BRANCH="agent/linux-expanded-kbuild-v0"
CONTRACT={
 "pi-local-symbol":("semantics",("[linux-runtime-pi-local-symbol-v0]","[runtime-compiler-semantics-v1]")),
 "satp-micro":("semantics",("[linux-pi-micro]","[runtime-compiler-semantics-v1]")),
 "qemu-watch-cert":("watch",("[linux-runtime-qemu-watch-cert-v1]","[linux-runtime-qemu-watch-contracts-v1]")),
 "inconclusive-cert":("watch",("[linux-runtime-qemu-watch-inconclusive-v1]","[linux-runtime-qemu-watch-contracts-v1]")),
}
def guard(mode, tags):
 manual=f"(github.event_name == 'workflow_dispatch' && (inputs.mode == '{mode}' || inputs.mode == 'all'))"
 tag_pred=" || ".join(f"contains(github.event.head_commit.message, '{tag}')" for tag in tags)
 push=f"(github.event_name == 'push' && github.ref_name == '{BRANCH}' && ({tag_pred}))"
 return manual+" || "+push

def selected(name, event, ref, mode="p1", message=""):
 relevant,tags=CONTRACT[name]
 if event=="workflow_dispatch":return mode in (relevant,"all")
 if event=="push":return ref==BRANCH and any(tag in message for tag in tags)
 return False

def main():
 c=WORK.read_text()
 for name,(mode,tags) in CONTRACT.items():
  m=re.search(r"(?m)^  "+re.escape(name)+r":\n    if: ([^\n]+)$",c)
  if m is None or m.group(1)!=guard(mode,tags):
   raise AssertionError(f"ambiguous/manual-silent-skip dispatch predicate: {name}")
  for branch in (BRANCH,"agent/linux-perf-boolean-domain-v1"):
   assert selected(name,"workflow_dispatch",branch,mode)
   assert selected(name,"workflow_dispatch",branch,"all")
   for other in ("p1","pi","trace","watch" if mode=="semantics" else "semantics"):
    assert not selected(name,"workflow_dispatch",branch,other)
   assert not selected(name,"push",branch,message="unrelated code")
  for tag in tags:
   assert selected(name,"push",BRANCH,message="trigger "+tag)
   assert not selected(name,"push","agent/linux-perf-boolean-domain-v1",message=tag)
 for mode in ("p1","pi","trace","semantics","watch","all"):
  if ("          - "+mode+"\n") not in c:
   raise AssertionError(f"manual choice missing: {mode}")
 print("M0_EARLY_RUNTIME_DISPATCH=PASS semantic_manual_perf=2 watch_manual_perf=2 runtime_push_tag_compat=8 independent_jobs=4")
if __name__=="__main__":main()
