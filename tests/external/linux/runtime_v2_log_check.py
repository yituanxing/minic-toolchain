#!/usr/bin/env python3
"""Validate Linux QEMU logs against the tiered runtime-v2 marker contract."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

PROFILE_ORDER = {"p0": 0, "p1": 1, "p2": 2, "p3": 3}
FATAL_PATTERNS = (
    r"Unknown symbol",
    r"invalid module format",
    r"\bOops:",
    r"\bBUG:",
    r"Kernel panic",
    r"soft lockup",
    r"workqueue lockup",
)

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=PROFILE_ORDER, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(__file__).with_name("runtime_v2_checks.tsv"),
    )
    return parser.parse_args()

def load_checks(path: Path, profile: str) -> list[tuple[str, str, str, str]]:
    selected: list[tuple[str, str, str, str]] = []
    ceiling = PROFILE_ORDER[profile]
    for raw in path.read_text().splitlines():
        if not raw or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) != 4:
            raise SystemExit(f"invalid runtime-v2 manifest row: {raw!r}")
        row_profile, check_id, pattern, fault_class = fields
        if row_profile not in PROFILE_ORDER:
            raise SystemExit(f"invalid profile in manifest: {row_profile}")
        if PROFILE_ORDER[row_profile] <= ceiling:
            selected.append((row_profile, check_id, pattern, fault_class))
    return selected

def main() -> int:
    args = parse_args()
    if not args.log.is_file():
        raise SystemExit(f"missing QEMU log: {args.log}")
    if not args.manifest.is_file():
        raise SystemExit(f"missing runtime manifest: {args.manifest}")

    text = args.log.read_text(errors="replace")
    fatal_hits = [pattern for pattern in FATAL_PATTERNS if re.search(pattern, text, re.I)]
    if fatal_hits:
        print(
            "LINUX_RUNTIME_V2_FAIL reason=fatal-markers "
            + " patterns="
            + ",".join(fatal_hits),
            file=sys.stderr,
        )
        return 1

    missing: list[tuple[str, str, str]] = []
    checks = load_checks(args.manifest, args.profile)
    for _, check_id, pattern, fault_class in checks:
        if re.search(pattern, text, re.M) is None:
            missing.append((check_id, fault_class, pattern))

    if args.profile == "p3":
        module_pass = len(re.findall(r"^MODULE_PASS\b", text, re.M))
        function_pass = len(re.findall(r"^FUNCTION_PASS\b", text, re.M))
        if module_pass != 145 or function_pass != 203:
            print(
                f"LINUX_RUNTIME_V2_FAIL profile=p3 module_pass={module_pass} "
                f"function_pass={function_pass} expected=145/203",
                file=sys.stderr,
            )
            return 1

    if missing:
        for check_id, fault_class, pattern in missing:
            print(
                f"LINUX_RUNTIME_V2_MISSING id={check_id} class={fault_class} "
                f"pattern={pattern}",
                file=sys.stderr,
            )
        print(
            f"LINUX_RUNTIME_V2_FAIL profile={args.profile} "
            f"missing={len(missing)} total={len(checks)}",
            file=sys.stderr,
        )
        return 1

    print(
        f"LINUX_RUNTIME_V2_PROFILE=PASS profile={args.profile} "
        f"checks={len(checks)} fatal=0"
    )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
