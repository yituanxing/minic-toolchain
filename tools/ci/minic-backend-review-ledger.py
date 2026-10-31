#!/usr/bin/env python3
"""Incremental MiniC backend review ledger, never equate CI with deep review.

Statuses are three independent axes: source snapshot, source inspection depth,
and touched-in-tested-PR evidence. Every sha is Git's blob sha, NOT an
unstable line number. Changed files reopen automatically without memory.
Run: python3 tools/ci/minic-backend-review-ledger.py [--changed|--summary|--check]
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "docs" / "architecture" / "minic-backend-review-ledger.json"
ALLOWED = {"inventory", "targeted", "full"}

def tracked():
    out = subprocess.check_output(
        ["git", "ls-files", "-z", "--", "src/target"],
        cwd=ROOT,
    )
    return sorted(
        x.decode("utf-8") for x in out.split(b"\0") if x
        and x.endswith((b".c", b".h"))
        # `git ls-files` includes deleted paths until the next commit.
        # A retired implementation must not be hashed as a live source.
        and (ROOT / x.decode("utf-8")).is_file()
    )

def blob(path):
    return subprocess.check_output(
        ["git", "hash-object", "--", path],
        cwd=ROOT, text=True,
    ).strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--changed", action="store_true",
                       help="only new/drifted source or unresolved audit modules")
    group.add_argument("--check", action="store_true",
                       help="CI: file inventory complete and manifest self-consistent")
    group.add_argument("--summary", action="store_true",
                       help="per-target summary and high-risk pending areas")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    reviewed = manifest["files"]
    present = set(tracked())
    known = set(reviewed)
    missing = sorted(present - known)
    removed = sorted(known - present)
    invalid = sorted(p for p, rec in reviewed.items()
                     if rec.get("review_depth") not in ALLOWED
                     or len(rec.get("blob", "")) != 40)
    stale = sorted(p for p in present & known
                   if blob(p) != reviewed[p]["blob"])
    stale_set = set(stale)
    state = {}
    for p in sorted(present):
        # A completed old source review cannot certify a different blob.
        # Do not count changed "full" files as still fully reviewed.
        depth = (reviewed[p]["review_depth"] if p in reviewed and
                 p not in stale_set else "new")
        key = p.split("/")[2]
        d = state.setdefault(key, dict(total=0, full=0, targeted=0,
                                      pending=0, changed=0))
        d["total"] += 1
        if p in stale_set:
            d["changed"] += 1
        if depth == "full":
            d["full"] += 1
        elif depth == "targeted":
            d["targeted"] += 1
        else:
            d["pending"] += 1
    if args.summary or (not args.check and not args.changed):
        print("MINIC_REVIEW_BASE", manifest["baseline_commit"])
        for group, d in sorted(state.items()):
            print("MINIC_REVIEW_SCOPE", group, json.dumps(d, sort_keys=True))
        print("MINIC_REVIEW_PRIORITY", ",".join(manifest["next_deep_review"]))
    if args.changed or args.check:
        print("MINIC_REVIEW_DELTA", json.dumps({
            "new": missing, "removed": removed,
            "stale": stale, "invalid": invalid,
        }, sort_keys=True))
    if args.changed:
        for p in stale:
            if reviewed[p]["review_depth"] == "full":
                print("MINIC_REVIEW_STALE_FULL", p)
        for p in manifest["next_deep_review"]:
            if p in present and (p in stale or
                                  reviewed[p]["review_depth"] != "full"):
                print("MINIC_REVIEW_NEXT", p,
                      "STALE" if p in stale else "PENDING")
    if args.check and (missing or removed or invalid):
        print("MINIC_REVIEW_MANIFEST=FAIL", file=sys.stderr)
        return 1
    if args.check:
        print("MINIC_REVIEW_MANIFEST=PASS inventory=%d drift=%d" %
              (len(present), len(stale)))
    return 0

if __name__ == "__main__":
    sys.exit(main())
