#!/usr/bin/env python3
"""Choose the minimum Runtime Owner Focused matrix for push or manual mode.

Only four explicit owner trigger files select narrow matrix lanes. Changes to
this workflow or this selector conservatively run every lane. Missing/ambiguous
push history also runs every lane rather than silently losing coverage.
"""
import argparse
import json
import re
import subprocess
import sys

MODES = ("timekeeping", "vsyscall", "notifier", "build-policy")
TRIGGERS = {
    "tools/ci/runtime-timekeeping-trigger.txt": "timekeeping",
    "tools/ci/runtime-vsyscall-trigger.txt": "vsyscall",
    "tools/ci/runtime-notifier-trigger.txt": "notifier",
    "tools/ci/runtime-build-policy-trigger.txt": "build-policy",
}
FULL_TRIGGER = {
    ".github/workflows/linux-runtime-owner-focused-v1.yml",
    "tools/ci/select_runtime_owner_modes_v1.py",
}

def choose(event, paths=(), manual_mode="all"):
    """Return ordered, nonempty modes; fail open if push path evidence is unclear."""
    if event == "workflow_dispatch":
        if manual_mode not in ("all",) + MODES:
            raise ValueError(f"unsupported manual mode: {manual_mode}")
        return list(MODES if manual_mode == "all" else (manual_mode,))
    if event != "push":
        raise ValueError(f"unsupported triggering event: {event}")
    changed = set(paths)
    if not changed or changed.intersection(FULL_TRIGGER):
        return list(MODES)
    unknown = changed.difference(TRIGGERS)
    if unknown:
        return list(MODES)
    return [m for m in MODES if m in {TRIGGERS[p] for p in changed}]

def diff_paths(before, after):
    if not re.fullmatch(r"[0-9a-fA-F]{40}", before or ""):
        return None
    if not re.fullmatch(r"[0-9a-fA-F]{40}", after or ""):
        return None
    if int(before, 16) == 0:
        return None
    try:
        subprocess.run(["git", "cat-file", "-e", before + "^{commit}"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        data = subprocess.check_output([
            "git", "diff", "--name-only", "--no-renames", "-z",
            "--diff-filter=ACMRD", before, after, "--",
        ], stderr=subprocess.DEVNULL)
        return [p.decode("utf-8", "surrogateescape") for p in data.split(b"\0") if p]
    except (subprocess.CalledProcessError, OSError):
        return None

def test():
    assert choose("workflow_dispatch", manual_mode="all") == list(MODES)
    for m in MODES:
        assert choose("workflow_dispatch", manual_mode=m) == [m]
    for file, mode in TRIGGERS.items():
        assert choose("push", [file]) == [mode]
    assert choose("push", list(TRIGGERS)[:2]) == list(MODES[:2])
    assert choose("push", list(TRIGGERS)) == list(MODES)
    assert choose("push", []) == list(MODES)
    for file in FULL_TRIGGER:
        assert choose("push", [file]) == list(MODES)
    assert choose("push", ["unreviewed-file"]) == list(MODES)
    assert choose("push", [list(TRIGGERS)[0], "unreviewed-file"]) == list(MODES)
    assert diff_paths("0"*40, "1"*40) is None
    for mode in ("oops", ""):
        try:
            choose("workflow_dispatch", manual_mode=mode)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid manual selection accepted")
    print("M0_RUNTIME_OWNER_ROUTING=PASS manual=5 push_single=4 push_combined=2 fail_open=4")

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--event", default="workflow_dispatch")
    p.add_argument("--mode", default="all")
    p.add_argument("--before", default="")
    p.add_argument("--after", default="")
    args = p.parse_args()
    if args.self_test:
        test()
        return
    changed = diff_paths(args.before, args.after) if args.event == "push" else []
    modes = choose(args.event, () if changed is None else changed, args.mode)
    print("ROUTE_EVENT=" + args.event, file=sys.stderr)
    print("ROUTE_CHANGED_PATHS=" + json.dumps(changed, ensure_ascii=False), file=sys.stderr)
    print("ROUTE_MODES=" + json.dumps(modes), file=sys.stderr)
    print("modes=" + json.dumps(modes, separators=(",", ":")))
if __name__ == "__main__":
    main()
