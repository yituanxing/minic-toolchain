#!/usr/bin/env python3
"""Route MiniObjcopy/Strip's T1 regression on shared ELF source changes.

Tags remain valid on unrelated pushes; an unavailable or ambiguous push diff
fails OPEN to the T1 regression, never to costly Linux integration jobs.
"""
import argparse
import re
import subprocess
import sys

TRIGGERS = frozenset({
    ".github/workflows/miniobjcopy-strip-regressions-v1.yml",
    "tools/ci/select_miniobjcopy_elf_route_v1.py",
})

def changed_files(before, after):
    if not all(re.fullmatch(r"[a-fA-F0-9]{40}", x or "") for x in (before, after)):
        return None
    if int(before, 16) == 0:
        return None
    try:
        for sha in (before, after):
            subprocess.run(["git", "cat-file", "-e", sha + "^{commit}"],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        data = subprocess.check_output(
            ["git", "diff", "--name-only", "--no-renames", "-z", before, after, "--"],
            stderr=subprocess.DEVNULL)
        return [x.decode("utf-8", "surrogateescape") for x in data.split(b"\0") if x]
    except (OSError, subprocess.CalledProcessError):
        return None

def needs_regression(event, changed):
    if event == "workflow_dispatch":
        return False  # Explicit manual modes are owned by the original job 'if'.
    if event != "push":
        raise ValueError(f"unsupported event: {event}")
    if changed is None:
        return True  # Never lose coverage on force-push or incomplete history.
    return any(p.startswith("elf/") or p in TRIGGERS for p in changed)

def self_test():
    assert needs_regression("push", ["elf/src/reader.c"])
    assert needs_regression("push", ["elf/src/binary_export.c"])
    assert needs_regression("push", ["elf/include/reader.h"])
    assert needs_regression("push", ["elf/src/reader.c", "docs/README.md"])
    assert needs_regression("push", list(TRIGGERS))
    assert needs_regression("push", None)
    assert not needs_regression("push", [])
    assert not needs_regression("push", ["tools/ci/runtime-timekeeping-trigger.txt"])
    assert not needs_regression("push", ["src/frontend/parser.c"])
    assert not needs_regression("push", ["docs/ci/README.md"])
    assert not needs_regression("workflow_dispatch", [])
    assert changed_files("0" * 40, "1" * 40) is None
    print("M0_MINIOBJCOPY_ELF_SELECTOR=PASS positive=5 negative=5 fail_open=1")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--event", default="workflow_dispatch")
    ap.add_argument("--before", default="")
    ap.add_argument("--after", default="")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return
    paths = changed_files(args.before, args.after) if args.event == "push" else []
    on = needs_regression(args.event, paths)
    print(f"ELF_ROUTE_EVENT={args.event} ELF_ROUTE_DIFF={'unknown' if paths is None else paths}", file=sys.stderr)
    print("elf_changed=" + str(on).lower())

if __name__ == "__main__":
    main()
