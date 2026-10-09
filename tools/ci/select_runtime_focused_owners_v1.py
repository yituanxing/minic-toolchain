#!/usr/bin/env python3
"""Route legacy IRQ/RCU/Init/Timer Runtime diagnostic push sentinels.

The four expensive historical diagnoses retain independent jobs, fixtures and
provenance. Workflow/selector maintenance is structural-only (M0); it must not
accidentally launch QEMU. Unknown push ancestry is explicitly inconclusive and
fails the routing job instead of falsely reporting a green skipped diagnosis.
"""
import argparse
import re
import subprocess
import sys

PATTERNS = {
    "irq": {"tools/ci/runtime-mm-core-trigger.txt",
            "tools/ci/runtime-init-irq-trigger.txt"},
    "rcu": {"tools/ci/linux-runtime-rest-init-frontier-v0.json"},
    "codegen": set(),  # Historical auto-push only on the retired YAML itself.
    "timer": {"tools/ci/runtime-timer-trigger.txt"},
}
SELF_FILES = {
    ".github/workflows/linux-runtime-focused-owners-v1.yml",
    "tools/ci/select_runtime_focused_owners_v1.py",
}

def changes(before, after):
    if not all(re.fullmatch(r"[a-f0-9]{40}", v or "", re.I) for v in (before, after)):
        return None
    if int(before, 16) == 0:
        return None
    try:
        for sha in (before, after):
            subprocess.run(["git", "cat-file", "-e", sha + "^{commit}"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           check=True)
        data = subprocess.check_output(
            ["git", "diff", "--name-only", "--no-renames", "-z",
             before, after, "--"], stderr=subprocess.DEVNULL)
        return [p.decode("utf-8", "surrogateescape")
                for p in data.split(b"\0") if p]
    except (OSError, subprocess.CalledProcessError):
        return None

def select(event, changed):
    if event == "workflow_dispatch":
        return {owner: False for owner in PATTERNS}
    if event != "push":
        raise ValueError("unsupported event")
    if changed is None:
        raise RuntimeError("push diff unavailable; manual owner dispatch required")
    return {owner: bool(set(changed) & patterns)
            for owner, patterns in PATTERNS.items()}

def self_test():
    expected = {
        "tools/ci/runtime-mm-core-trigger.txt": ("irq",),
        "tools/ci/runtime-init-irq-trigger.txt": ("irq",),
        "tools/ci/linux-runtime-rest-init-frontier-v0.json": ("rcu",),
        "tools/ci/runtime-timer-trigger.txt": ("timer",),
    }
    for src, owners in expected.items():
        out = select("push", [src])
        assert tuple(k for k,v in out.items() if v) == owners, (src,out)
    combo = select("push",["tools/ci/runtime-init-irq-trigger.txt",
                            "tools/ci/runtime-timer-trigger.txt"])
    assert combo == {"irq": True, "rcu": False, "codegen": False, "timer": True}
    for ignored in (
        [], ["docs/ci/README.md"], list(SELF_FILES),
        ["src/frontend/parser.c"], ["tools/ci/select_runtime_owner_modes_v1.py"]
    ):
        assert not any(select("push", ignored).values())
    assert not any(select("workflow_dispatch", None).values())
    try:
        select("push",None)
        raise AssertionError("unknown diff should fail rather than fake a PASS")
    except RuntimeError:
        pass
    assert changes("0"*40, "1"*40) is None
    print("M0_RUNTIME_FOCUSED_OWNER_ROUTE=PASS old_sentinels=4 owners=4 multi_owner=1 structural_only=1 manual=1 fail_closed=1")

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--event", default="workflow_dispatch")
    p.add_argument("--before",default="")
    p.add_argument("--after",default="")
    p.add_argument("--self-test",action="store_true")
    args=p.parse_args()
    if args.self_test:
        self_test()
        return
    changed=changes(args.before,args.after) if args.event=="push" else []
    try:
        lanes=select(args.event,changed)
    except RuntimeError as exc:
        print("RUNTIME_FOCUSED_ROUTING=INCONCLUSIVE "+str(exc),file=sys.stderr)
        sys.exit(2)
    print("RUNTIME_FOCUSED_CHANGED="+repr(changed),file=sys.stderr)
    for owner,value in lanes.items():
        print(owner+"="+str(value).lower())

if __name__=="__main__":
    main()
