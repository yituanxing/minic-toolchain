#!/usr/bin/env python3
"""Check all 38 maintained workflow entries against the owned trigger inventory.

This is a T0 source/dispatch audit, not proof of T1-T4 execution. In particular
a push path filter only creates a workflow run; job-level 'if' may skip tests.
"""
from pathlib import Path
import ast
import csv
import fnmatch
import os
import re

ROOT=Path(__file__).resolve().parents[2]
BRANCHES=("agent/linux-expanded-kbuild-v0","agent/linux-perf-boolean-domain-v1")
INV=ROOT/"docs/ci/active-workflow-inventory-2026-10-09.tsv"

def header(y):
    return y.split("\njobs:\n",1)[0]

def push_patterns(y):
    h=header(y)
    m=re.search(r"(?m)^    paths:\n((?:      - [^\n]*\n)+)",h)
    if not m: return None
    return [ast.literal_eval(s.strip()[2:].strip()) for s in m.group(1).splitlines()]

def accepts(patterns,path):
    if patterns is None: return True
    accept=False
    for glob in patterns:
        neg=glob.startswith("!")
        if fnmatch.fnmatchcase(path,glob[1:] if neg else glob):
            accept=not neg
    return accept

def main():
    with INV.open(newline="") as f:
        rows=list(csv.DictReader(f,delimiter="\t"))
    if len(rows)!=38 or len({r["path"] for r in rows})!=26:
        raise AssertionError("38-row / 26-name owned inventory changed without review")
    branch=os.environ.get("GITHUB_REF_NAME",BRANCHES[0])
    if branch not in BRANCHES:
        raise AssertionError(f"unknown CI branch: {branch}")
    local={r["path"]:r for r in rows if r["branch"]==branch}
    if len(local) != (25 if branch==BRANCHES[0] else 13):
        raise AssertionError("incorrect owner workflow count")
    actual={str(p.relative_to(ROOT)) for p in (ROOT/".github/workflows").glob("*.yml")}
    if set(local)!=actual:
        raise AssertionError(f"inventory/source divergence missing={sorted(actual-set(local))} extra={sorted(set(local)-actual)}")
    for path,row in local.items():
        y=(ROOT/path).read_text()
        if "\njobs:\n" not in y:
            raise AssertionError(f"missing jobs: {path}")
        jobs=set(re.findall(r"(?m)^  ([a-zA-Z][\w-]*):\s*$",y.split("\njobs:\n",1)[1]))
        recorded=set(row["job_ids"].split(";"))
        if jobs!=recorded:
            raise AssertionError(f"job routing drift in {path}: expected={recorded} actual={jobs}")
        if ("  push:" in header(y)) != ("push" in row["trigger_types"].split(";")):
            raise AssertionError(f"push contract drift in {path}")
    for owner in ("toolchain-focused-regressions-v1.yml",):
        y=(ROOT/".github/workflows"/owner).read_text()
        p=push_patterns(y)
        if p is None: raise AssertionError(f"missing positive triggers: {owner}")
        for source in ("elf/src/reader.c","elf/src/relocatable_writer.c","elf/src/rewrite.c",
                       "src/frontend/parse.c","archiver/miniar.c","linker/minild.c"):
            if not accepts(p,source): raise AssertionError(f"MISS {owner} {source}")
        for excluded in ("tools/ci/runtime-timekeeping-trigger.txt",
                         "tools/ci/select_miniobjcopy_elf_route_v1.py",
                         "tools/ci/check_ci_trigger_coverage_v1.py",
                         "tools/ci/linux-runtime-build-minic-profile-v1.sh",
                         "tools/ci/select_runtime_focused_owners_v1.py"):
            if accepts(p,excluded):
                raise AssertionError(f"OVERTRIGGER {owner} maintenance-only source: {excluded}")
    mini=(ROOT/".github/workflows/miniobjcopy-strip-regressions-v1.yml").read_text()
    if "needs.route.outputs.elf_changed == 'true'" not in mini:
        raise AssertionError("MiniObjcopy T1 is not ELF push routed")
    if "contains(github.event.head_commit.message, '[miniobjcopy-linux-tool]')" not in mini:
        raise AssertionError("MiniObjcopy Linux integration tag lost")
    if "contains(github.event.head_commit.message, '[miniobjcopy-linux-image]')" not in mini:
        raise AssertionError("MiniObjcopy Image integration tag lost")
    cpp=(ROOT/".github/workflows/minipp-a0.yml").read_text()
    if not accepts(push_patterns(cpp),"elf/src/reader.c"):
        raise AssertionError("MiniPP A0 shared ELF dependency skipped")
    # Runtime-only tag-gated regressions are not a Performance correctness gate.
    if branch==BRANCHES[1]:
        rv=(ROOT/".github/workflows/toolchain-focused-regressions-v1.yml").read_text()
        if "      - \"agent/linux-perf-boolean-domain-v1\"" not in header(rv):
            raise AssertionError("Performance source changes do not trigger MiniC RV64 T1")
        if set(re.findall(r"(?m)^  ([a-zA-Z][\w-]*):\s*$",rv.split("\njobs:\n",1)[1])) != {"focused-t1"}:
            raise AssertionError("Performance shared T1 must have exactly one build-and-verdict job")
        for owner in ("minic-rv64","miniar","minild"):
            if "inputs.mode == '"+owner+"'" not in rv:
                raise AssertionError(f"Performance selected T1 mode lost: {owner}")
        for mark in ("TOOLCHAIN_FOCUSED_T1=PASS","TOOLCHAIN_FOCUSED_T1=FAIL","steps.ld_gc.outcome","steps.ar_nm.outcome","steps.rv64.outcome"):
            if mark not in rv: raise AssertionError("Performance independent oracles lost: "+mark)
        print("M0_PERF_T1=IN_SCOPE owners=3 shared_build_jobs=1 (real T1 execution separately required)")
    print(f"M0_CI_TRIGGER_LEDGER=PASS branch={branch} maintained={len(local)} total=38 unique=26 route_checks=4 tier=T0")

if __name__=="__main__":
    main()
