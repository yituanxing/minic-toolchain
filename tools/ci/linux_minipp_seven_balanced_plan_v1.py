#!/usr/bin/env python3
"""One SHA-bound, fail-closed owner schedule shared by seven native producers and merger.

The committed 2064-owner plan was generated offline by descending historical
four-stage process time (LPT greedy with maximum 295 objects per worker).
It is used ONLY on explicit opt-in; the legacy stride allocation is untouched.
The historical figures predict work distribution, not GitHub runner wall time.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def balanced_members(owners: list[str], plan_path: Path) -> list[list[str]]:
    """Return seven owner lists in canonical original order; reject stale plans."""
    plan_path = Path(plan_path)
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    if plan.get("schema") != "minipp-native-balanced-seven-plan-v1":
        raise ValueError("balanced plan schema mismatch")
    if plan.get("owner_count") != 2064 or len(owners) != 2064:
        raise ValueError("balanced owner count must be exactly 2064")
    if len(set(owners)) != len(owners) or any(
        PurePosixPath(s).is_absolute() or ".." in PurePosixPath(s).parts
        or len(PurePosixPath(s).parts) < 2 or not s.endswith(".o")
        or "\\" in s for s in owners
    ):
        raise ValueError("invalid or duplicate balanced owner path")
    order = sorted(owners)
    manifest_digest = sha256(("\n".join(order) + "\n").encode("utf-8"))
    if manifest_digest != plan.get("sorted_owner_sha256"):
        raise ValueError("balanced plan does not match actual 2064-owner universe")
    digits = plan.get("digits_by_sorted_owner")
    if (not isinstance(digits, str) or len(digits) != len(owners)
            or any(ch not in "0123456" for ch in digits)
            or sha256(digits.encode("ascii")) != plan.get("assignment_sha256")):
        raise ValueError("balanced plan assignment tampered or invalid")
    assigned = dict(zip(order, map(int, digits)))
    groups = [[owner for owner in owners if assigned[owner] == shard]
              for shard in range(7)]
    counts = [len(group) for group in groups]
    if counts != [295, 295, 294, 295, 295, 295, 295]:
        raise ValueError(f"unbalanced shard counts: {counts}")
    flat = [owner for group in groups for owner in group]
    if len(flat) != len(owners) or set(flat) != set(owners):
        raise ValueError("missing or duplicated owners in balanced plan")
    old = plan.get("old_shard_ms")
    new = plan.get("forecast_shard_ms")
    if (old != [714578, 855220, 772024, 879790, 744647, 427654, 781140]
            or new != [739298, 739298, 739291, 739297, 739290, 739290, 739289]
            or sum(old) != sum(new) or plan.get("reference_run") != 38870185276):
        raise ValueError("balanced plan cost evidence mismatch")
    return groups


def main() -> None:
    arg = argparse.ArgumentParser()
    arg.add_argument("--plan", type=Path, required=True)
    arg.add_argument("--owners", type=Path, required=True)
    args = arg.parse_args()
    names = [line.strip() for line in args.owners.read_text().splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    groups = balanced_members(names, args.plan)
    # Probe noncanonical order too; the mapping must remain identical by name.
    flipped = balanced_members(list(reversed(names)), args.plan)
    assert all(set(a) == set(b) for a, b in zip(groups, flipped))
    # Neither a missing nor an extra owner may silently receive another slot.
    try:
        balanced_members(names[:-1], args.plan)
    except ValueError:
        pass
    else:
        raise AssertionError("balanced plan accepted missing owner")
    try:
        balanced_members(names[:-1] + ["kernel/bogus_balanced_owner.o"], args.plan)
    except ValueError:
        pass
    else:
        raise AssertionError("balanced plan accepted alien owner")
    print("MINIPP_BALANCED_PLAN_SELFTEST=PASS owners=2064 shards=7 "
          "counts=" + ",".join(str(len(g)) for g in groups) +
          " original_universe_sha_bound=true missing_and_extra_rejected=true")


if __name__ == "__main__":
    main()
