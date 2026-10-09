#!/usr/bin/env python3
"""Verify exactly which Linux C object builds completed through MiniC.

Input successes must come from stage2_kbuild_cc.sh *after* both MiniC -S and
GNU assembly succeeded. This is stronger than merely observing that each .o
exists, which might silently certify stale or GCC-built objects as MiniC.
"""
from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path, PurePosixPath
import sys


def clean_name(raw: str) -> str:
    name = raw.strip()
    p = PurePosixPath(name)
    if not name or p.is_absolute() or ".." in p.parts or len(p.parts) < 2 or not name.endswith(".o"):
        raise ValueError(f"invalid target in route evidence: {raw!r}")
    return name


def check(objects: list[str], successes: list[str]) -> tuple[int, int]:
    expected = list(map(clean_name, objects))
    observed = list(map(clean_name, successes))
    if not expected or len(expected) != len(set(expected)):
        raise ValueError("missing or duplicate target in reference list")
    counts = Counter(observed)
    missing = [x for x in expected if counts[x] == 0]
    duplicate = [x for x in expected if counts[x] != 1 and counts[x] != 0]
    if missing or duplicate:
        raise ValueError(
            f"MiniC route incomplete: expected={len(expected)} observed={len(observed)} "
            f"missing={len(missing)} {missing[:12]} duplicate={len(duplicate)} {duplicate[:12]}"
        )
    unexpected = set(counts) - set(expected)
    return len(expected), len(unexpected)


def load_names(path: Path) -> list[str]:
    return [l.strip() for l in path.read_text(encoding="utf-8").splitlines()
            if l.strip() and not l.lstrip().startswith("#")]


def self_test() -> None:
    a, b = ["kernel/foo.o", "mm/bar.o"], ["mm/bar.o", "kernel/foo.o"]
    assert check(a, b) == (2, 0)
    assert check(a, b + ["drivers/side.o"]) == (2, 1)
    for observed in (b[:1], b + ["kernel/foo.o"], ["kernel/foo.o", "../escape.o"]):
        try:
            check(a, observed)
        except ValueError:
            continue
        raise AssertionError("route audit accepted missing, duplicate or unsafe evidence")
    print("MINIC_ROUTE_AUDIT_SELFTEST=PASS cases=5")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--objects-file", type=Path)
    parser.add_argument("--success-trace", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.self_test:
            self_test()
            return 0
        if args.objects_file is None or args.success_trace is None:
            parser.error("--objects-file and --success-trace are required")
        count, extra = check(load_names(args.objects_file), load_names(args.success_trace))
    except (ValueError, OSError, AssertionError) as exc:
        print(f"MINIC_ROUTE_AUDIT=FAIL reason={exc}", file=sys.stderr)
        return 2
    print(f"MINIC_ROUTE_AUDIT=PASS minic_compiled={count} ancillary_minic_compiled={extra}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
