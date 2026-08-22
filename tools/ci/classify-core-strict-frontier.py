#!/usr/bin/env python3
"""Classify a frozen Linux Core-strict replay by first unsupported function."""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import argparse
import json
import re

UNSUPPORTED_RE = re.compile(r"^Core IR shadow does not yet support function '([^']+)'$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("results", type=Path)
    parser.add_argument("--examples", type=int, default=5)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--require-no-errors", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    rows = json.loads(args.results.read_text())
    unsupported: dict[str, list[dict[str, object]]] = defaultdict(list)
    errors: list[dict[str, object]] = []
    missing: list[dict[str, object]] = []
    passes = 0

    for row in rows:
        status = str(row.get("status", ""))
        if status == "PASS":
            passes += 1
            continue
        if status == "PREPROCESS_MISSING":
            missing.append(row)
            continue
        message = str(row.get("message", ""))
        match = UNSUPPORTED_RE.match(message)
        if match:
            unsupported[match.group(1)].append(row)
        else:
            errors.append(row)

    ranked = sorted(unsupported.items(), key=lambda item: (-len(item[1]), item[0]))
    print(
        "CORE_STRICT500_SUMMARY "
        f"selected={len(rows)} pass={passes} "
        f"unsupported={sum(len(group) for group in unsupported.values())} "
        f"error={len(errors)} preprocess_missing={len(missing)} "
        f"distinct_blockers={len(ranked)}"
    )
    for rank, (function, group) in enumerate(ranked, 1):
        print(f"CORE_STRICT500_BLOCKER rank={rank} count={len(group)} function={function}")
        for row in group[: max(0, args.examples)]:
            print(
                "CORE_STRICT500_EXAMPLE "
                f"function={function} index={row['index']} input={row['input']}"
            )
    for row in errors:
        print(
            "CORE_STRICT500_ERROR "
            f"index={row.get('index')} input={row.get('input')} message={row.get('message')}"
        )
    for row in missing:
        print(
            "CORE_STRICT500_MISSING "
            f"index={row.get('index')} input={row.get('input')}"
        )

    summary = {
        "selected": len(rows),
        "pass": passes,
        "unsupported": sum(len(group) for group in unsupported.values()),
        "error": len(errors),
        "preprocess_missing": len(missing),
        "blockers": [
            {
                "function": function,
                "count": len(group),
                "indices": [row["index"] for row in group],
                "inputs": [row["input"] for row in group],
            }
            for function, group in ranked
        ],
        "errors": errors,
        "missing": missing,
    }
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

    if args.require_no_errors and (errors or missing):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
