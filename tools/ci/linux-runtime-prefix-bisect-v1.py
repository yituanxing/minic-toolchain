#!/usr/bin/env python3
"""Plan a safe ordered-prefix bisection over Linux runtime objects.

The tool does not decide what a runtime verdict means.  Its caller records each
prefix as PASS, FAIL, or INCONCLUSIVE according to the active runtime gate.  We
only maintain the bisection bracket, reject non-monotonic evidence, and identify
the first object when adjacent PASS/FAIL prefixes are proven.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys

SCHEMA = "minic-linux-runtime-prefix-bisect-v1"
VALID = {"PASS", "FAIL", "INCONCLUSIVE"}


class BisectError(RuntimeError):
    pass


def normalize_object_path(raw: str) -> str:
    value = raw.strip().replace("\\", "/")
    if not value:
        raise BisectError("empty object path")
    p = PurePosixPath(value)
    if p.is_absolute() or any(part in ("", ".", "..") for part in p.parts):
        raise BisectError(f"unsafe object path: {raw!r}")
    value = p.as_posix()
    if not value.endswith(".o"):
        raise BisectError(f"object path must end in .o: {raw!r}")
    return value


def load_objects(path: Path) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        obj = normalize_object_path(line)
        if obj in seen:
            raise BisectError(f"duplicate object at {path}:{lineno}: {obj}")
        seen.add(obj)
        result.append(obj)
    if not result:
        raise BisectError(f"object list is empty: {path}")
    return result


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_observations(values: list[str], n: int) -> dict[int, str]:
    out: dict[int, str] = {}
    for raw in values:
        if "=" not in raw:
            raise BisectError(f"observation must be PREFIX=VERDICT: {raw!r}")
        prefix_text, verdict_text = raw.split("=", 1)
        try:
            prefix = int(prefix_text, 10)
        except ValueError as exc:
            raise BisectError(f"invalid prefix in observation: {raw!r}") from exc
        verdict = verdict_text.strip().upper()
        if prefix < 0 or prefix > n:
            raise BisectError(f"observed prefix out of range 0..{n}: {prefix}")
        if verdict not in VALID:
            raise BisectError(f"invalid verdict {verdict!r}; expected PASS/FAIL/INCONCLUSIVE")
        previous = out.get(prefix)
        if previous is not None and previous != verdict:
            raise BisectError(
                f"conflicting observations for prefix {prefix}: {previous} vs {verdict}"
            )
        out[prefix] = verdict
    return out


def plan(objects: list[str], observations: dict[int, str], objects_sha: str) -> dict:
    n = len(objects)
    passes = sorted(prefix for prefix, verdict in observations.items() if verdict == "PASS")
    fails = sorted(prefix for prefix, verdict in observations.items() if verdict == "FAIL")

    max_pass = max(passes) if passes else None
    min_fail = min(fails) if fails else None

    # A prefix model is only valid while all PASS evidence is before all FAIL evidence.
    if max_pass is not None and min_fail is not None and max_pass >= min_fail:
        return {
            "schema": SCHEMA,
            "status": "NON_MONOTONIC",
            "object_count": n,
            "objects_sha256": objects_sha,
            "observations": {str(k): observations[k] for k in sorted(observations)},
            "max_pass": max_pass,
            "min_fail": min_fail,
            "reason": "PASS/FAIL evidence violates ordered-prefix monotonicity",
        }

    # Establish the two required endpoints rather than silently assuming them.
    if max_pass is None:
        next_prefix = 0
        status = "RETRY" if observations.get(0) == "INCONCLUSIVE" else "NEXT"
        reason = "prove GCC baseline prefix 0"
    elif min_fail is None:
        next_prefix = n
        status = "RETRY" if observations.get(n) == "INCONCLUSIVE" else "NEXT"
        reason = "prove an all-candidate failing endpoint"
    elif min_fail == max_pass + 1:
        return {
            "schema": SCHEMA,
            "status": "ISOLATED",
            "object_count": n,
            "objects_sha256": objects_sha,
            "observations": {str(k): observations[k] for k in sorted(observations)},
            "max_pass": max_pass,
            "min_fail": min_fail,
            "first_bad_prefix": min_fail,
            "first_bad_index": min_fail,
            "first_bad_object": objects[min_fail - 1],
        }
    else:
        next_prefix = (max_pass + min_fail) // 2
        # INCONCLUSIVE is deliberately not evidence. Retry the exact midpoint.
        status = "RETRY" if observations.get(next_prefix) == "INCONCLUSIVE" else "NEXT"
        reason = "retry inconclusive midpoint" if status == "RETRY" else "bisect bracket"

    return {
        "schema": SCHEMA,
        "status": status,
        "object_count": n,
        "objects_sha256": objects_sha,
        "observations": {str(k): observations[k] for k in sorted(observations)},
        "max_pass": max_pass,
        "min_fail": min_fail,
        "next_prefix": next_prefix,
        "next_object": objects[next_prefix - 1] if next_prefix > 0 else None,
        "reason": reason,
    }


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--objects-file", type=Path, required=True)
    p.add_argument(
        "--observe",
        action="append",
        default=[],
        metavar="PREFIX=VERDICT",
        help="record PASS, FAIL, or INCONCLUSIVE for an exact prefix",
    )
    p.add_argument("--json-out", type=Path)
    return p


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        objects = load_objects(args.objects_file)
        observations = parse_observations(args.observe, len(objects))
        result = plan(objects, observations, file_sha256(args.objects_file))
        text = json.dumps(result, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(text, encoding="utf-8")
        sys.stdout.write(text)
        return 3 if result["status"] == "NON_MONOTONIC" else 0
    except (BisectError, OSError) as exc:
        print(f"PREFIX_BISECT_ERROR={exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
