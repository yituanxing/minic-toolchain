#!/usr/bin/env python3
"""M0: exact archived provenance and 4=>1 Runtime focused diagnostic contracts."""
from pathlib import Path
import hashlib
import os
import re

ROOT=Path(__file__).resolve().parents[2]
BRANCHES=("agent/linux-expanded-kbuild-v0","agent/linux-perf-boolean-domain-v1")
NAMES=("linux-runtime-mm-core-frontier-v0.yml",
       "linux-runtime-rcu-owner-v0.yml",
       "linux-runtime-riscv-init-codegen-v0.yml",
       "linux-runtime-timer-focused-v0.yml")
JOBS=("init-irq-bridge","rcu-softirq-owner-frontier",
      "riscv-init-codegen","timer-frontier")
MODES=("irq","rcu","codegen","timer")
SHAS=("27d159c6c54cc01bff9a07f720ddd9ef00f08976",
      "33e93692052a2a4457b7cc26d14a6e5c9b4fc11a",
      "5355fd4ffad244b9c415c567d37387d2f3dfb1f4",
      "d17ddd8d66943fffaccd870dcb0c7a5742a9fcca")
CANON=ROOT/".github/workflows/linux-runtime-focused-owners-v1.yml"
SELECTOR=ROOT/"tools/ci/select_runtime_focused_owners_v1.py"

def git_sha(buf):
    return hashlib.sha1(b"blob "+str(len(buf)).encode()+b"\0"+buf).hexdigest()

def body(txt,job):
    s=txt.split("\njobs:\n",1)[1]
    m=re.search(r"(?ms)^  "+re.escape(job)+r":\n(.*?)(?=^  [a-z][\w-]*:\n|\Z)",s)
    if m is None:
        raise AssertionError("job missing: "+job)
    return m.group(1)

def main():
    branch=os.environ.get("GITHUB_REF_NAME",BRANCHES[0])
    if branch not in BRANCHES:
        raise AssertionError("unexpected development branch "+branch)
    text=CANON.read_text()
    header=text.split("\njobs:\n",1)[0]
    if 'branches: ["agent/linux-expanded-kbuild-v0"]' not in header:
        raise AssertionError("changed old Runtime-only automatic branch scope")
    if '  workflow_dispatch:' not in header or '        default: codegen' not in header:
        raise AssertionError("safe manual default or manual mode missing")
    if "          - all" not in header or "          - timer" not in header:
        raise AssertionError("manual owner selection missing")
    live=set(re.findall(r"(?m)^  ([a-z][\w-]*):\s*$",
                        text.split("\njobs:\n",1)[1]))
    if live!=set(("route",)+JOBS):
        raise AssertionError(f"lost separate diagnosis: {live}")
    if "fetch-depth: 0" not in body(text,"route"):
        raise AssertionError("push routing needs ancestry for accurate diff")
    if "select_runtime_focused_owners_v1.py" not in body(text,"route"):
        raise AssertionError("routing script missing from new owner")
    for name,job,mode,sha in zip(NAMES,JOBS,MODES,SHAS):
        if (ROOT/".github/workflows"/name).exists():
            raise AssertionError("legacy diagnosis still active: "+name)
        raw=(ROOT/".github/workflows-disabled"/name).read_bytes()
        if git_sha(raw)!=sha:
            raise AssertionError("original immutable YAML blob mismatch: "+name)
        old=raw.decode("utf-8")
        marker="\njobs:\n  "+job+":\n"
        if old.count(marker)!=1:
            raise AssertionError("original job signature mutated: "+name)
        oldbody=old.split(marker,1)[1].rstrip("\n")
        grouping=re.search(r"(?m)^  group: (.+)$",old)
        if grouping is None or "  cancel-in-progress: true" not in old:
            raise AssertionError("original cancellation boundary changed: "+name)
        prefix=(
          "    needs: route\n"
          "    if: (github.event_name == 'workflow_dispatch' && "
          "(inputs.mode == 'all' || inputs.mode == '"+mode+"')) "
          "|| (github.event_name == 'push' && needs.route.outputs."+mode+" == 'true')\n"
          "    concurrency:\n"
          "      group: "+grouping.group(1)+"\n"
          "      cancel-in-progress: true\n"
        )
        found=body(text,job)
        if not found.startswith(prefix):
            raise AssertionError("push/manual/independent cancellation changed: "+name)
        if found[len(prefix):].rstrip("\n")!=oldbody:
            raise AssertionError("original executable diagnostic job changed: "+name)
    # Source/selector edits must enter lightweight structural routing only:
    if ".github/workflows/linux-runtime-focused-owners-v1.yml" not in header:
        raise AssertionError("owner YAML edits do not reach cheap route")
    if "tools/ci/select_runtime_focused_owners_v1.py" not in header:
        raise AssertionError("selector edits do not reach cheap route")
    print(f"M0_RUNTIME_4_TO_1=PASS branch={branch} historical_blobs=4 independent_jobs=4 original_test_bodies=4 old_cancel_groups=4 runtime_push_scope=1")

if __name__=="__main__":
    main()
