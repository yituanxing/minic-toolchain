#!/usr/bin/env python3
"""Read-only T0 governance for the four explicitly owned remote branches.

This is a drift *detector*, not a GitHub branch-creation restriction. Admin
repository rulesets are still needed to prevent unapproved remote ref creation.
Never delete refs or mutate the remote from this check.
"""
from __future__ import annotations
import argparse
import re
import subprocess
import time

ALLOWED = frozenset({
    "main",
    "agent/linux-expanded-kbuild-v0",
    "agent/linux-perf-boolean-domain-v1",
    "archive/all-progress-2026-10-04",
})

def parse_remote(stdout: str) -> set[str]:
    names = set()
    for line in stdout.splitlines():
        parts = line.split("\t")
        if len(parts) != 2 or re.fullmatch(r"[0-9a-fA-F]{40}", parts[0]) is None:
            raise AssertionError(f"invalid remote branch advertisement: {line!r}")
        if not parts[1].startswith("refs/heads/"):
            raise AssertionError(f"unexpected advertised ref: {parts[1]!r}")
        name = parts[1][len("refs/heads/"):]
        if not name or name in names:
            raise AssertionError("empty/duplicate remote branch name")
        names.add(name)
    return names

def verify(names: set[str]) -> None:
    if names != ALLOWED:
        raise AssertionError("unapproved remote refs: extra="
                             + repr(sorted(names - ALLOWED))
                             + " missing=" + repr(sorted(ALLOWED - names)))

def self_test() -> None:
    sample = "".join("a"*40 + "\trefs/heads/" + name + "\n" for name in sorted(ALLOWED))
    assert parse_remote(sample) == ALLOWED
    verify(parse_remote(sample))
    for names in (ALLOWED - {"main"}, ALLOWED | {"agent/unreviewed"}):
        try:
            verify(names)
        except AssertionError:
            pass
        else:
            raise AssertionError("branch drift was not detected")
    print("M0_BRANCH_POLICY_SELFTEST=PASS exact=4 unknown_branch=blocked missing_branch=blocked")

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return
    # GitHub can transiently fail a read-only remote listing. Retry instead
    # of allowing network jitter to masquerade as a source correctness error.
    error = None
    for attempt in range(3):
        try:
            raw = subprocess.check_output(
                ["git", "ls-remote", "--heads", "origin"],
                text=True, timeout=30, stderr=subprocess.PIPE,
            )
            break
        except (OSError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
            error = exc
            if attempt < 2:
                time.sleep(2)
    else:
        raise RuntimeError("cannot verify remote branch inventory after 3 read attempts") from error
    names = parse_remote(raw)
    verify(names)
    print("M0_BRANCH_POLICY=PASS permanent_remote_refs=4 source_only=1")

if __name__ == "__main__":
    main()
