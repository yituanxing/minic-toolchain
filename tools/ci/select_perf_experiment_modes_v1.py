#!/usr/bin/env python3
"""Select only the Performance experiment jobs whose original push paths match.

A missing/force-push diff conservatively enables every auto-selected experiment
(the First500 job still requires its historical [perf-first500] commit tag).
Do not treat a routing PASS as a test PASS.
"""
import argparse
import fnmatch
import re
import subprocess
import sys

PATTERNS = {
    "top5": (
        "tools/ci/apply-perf-core-object-interval-onepass-v1.py",
    ),
    "ice": (
        "tests/compiler/c0/gnu_constant_p_ice_regression.c",
        "tests/compiler/c0/gnu_choose_dynamic_condition_reject.c",
        "tools/ci/apply-correctness-constant-p-ice-v1.py",
    ),
    "first500": (
        "src/**", "include/**",
        "tools/ci/linux-runtime-build-minic-profile-v1.sh",
        "tools/ci/linux-perf-*", "tools/ci/apply-perf-*",
        "tools/ci/apply-inline-specialization-*",
        "tools/ci/apply-correctness-*",
        "tools/ci/perf-v1-overrides/**",
        "tools/minic-cc/**", "tools/minic/**",
        "Makefile", "tests/compiler/**",
    ),
    "parser": (
        "src/frontend/**", "include/**",
        "tools/ci/apply-perf-parser-*",
        "tools/ci/apply-inline-specialization-local-boolean-domain-scoped-v1.py",
        "tools/ci/linux-runtime-build-minic-profile-v1.sh",
        "tests/compiler/c0/**",
    ),
}

def paths_between(before, after):
    if not all(re.fullmatch(r"[0-9a-fA-F]{40}", v or "") for v in (before, after)):
        return None
    if int(before, 16) == 0:
        return None
    try:
        for sha in before, after:
            subprocess.run(["git", "cat-file", "-e", sha+"^{commit}"],
                           check=True, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
        output=subprocess.check_output(
            ["git","diff","--name-only","--no-renames","-z",before,after,"--"],
            stderr=subprocess.DEVNULL)
        return [b.decode("utf-8","surrogateescape") for b in output.split(b"\0") if b]
    except (subprocess.CalledProcessError,OSError):
        return None

def select(event, changed):
    if event == "workflow_dispatch":
        return {owner: False for owner in PATTERNS}
    if event != "push":
        raise ValueError("unsupported event: "+event)
    if changed is None:
        return {owner: True for owner in PATTERNS}
    return {
        owner: any(fnmatch.fnmatchcase(path, pattern)
                   for path in changed for pattern in patterns)
        for owner, patterns in PATTERNS.items()
    }

def self_test():
    for own,path in (
        ("top5","tools/ci/apply-perf-core-object-interval-onepass-v1.py"),
        ("ice","tests/compiler/c0/gnu_constant_p_ice_regression.c"),
        ("parser","src/frontend/parser.c"),
        ("first500","src/codegen/core.c"),
    ):
        assert select("push",[path])[own]
    assert select("push",["tests/compiler/c0/gnu_constant_p_ice_regression.c"]) == {
        "top5":False,"ice":True,"first500":True,"parser":True
    }
    assert select("push",["tools/ci/apply-perf-core-object-interval-onepass-v1.py"]) == {
        "top5":True,"ice":False,"first500":True,"parser":False
    }
    for paths in ([],["docs/ci/README.md"],
                  [".github/workflows/linux-performance-experiments-v1.yml"],
                  ["tools/ci/select_perf_experiment_modes_v1.py"]):
        assert not any(select("push",paths).values())
    assert all(select("push",None).values())
    assert not any(select("workflow_dispatch",None).values())
    assert paths_between("0"*40,"1"*40) is None
    print("M0_PERF_EXPERIMENT_ROUTING=PASS auto_owners=4 overlap=2 negative=4 fail_open=1 manual=1")

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--event",default="workflow_dispatch")
    p.add_argument("--before",default="")
    p.add_argument("--after",default="")
    p.add_argument("--self-test",action="store_true")
    args=p.parse_args()
    if args.self_test:
        self_test();return
    paths=paths_between(args.before,args.after) if args.event=="push" else []
    outcomes=select(args.event,paths)
    print("PERF_ROUTE_PATHS="+("UNKNOWN" if paths is None else repr(paths)),file=sys.stderr)
    for owner,run in outcomes.items():
        print(owner+"="+str(run).lower())

if __name__=="__main__":
    main()
