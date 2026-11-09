#!/usr/bin/env python3
"""Fail-closed MiniC -S acceptance gate for the 21-shard Linux census.

This consumes the *aggregate* produced by linux-three-isa-summary-v1.py,
whose producer has already checked all 21 shard receipts, original GNU owner
identity, input SHA-256 and one MiniC binary.  This final gate deliberately
does not change the collector: the collector must report *every* failed TU.

Passing here certifies only complete MiniC C-to-assembly coverage.  GNU AS,
AR, LD, Image creation and QEMU runtime require separate acceptance gates.
"""

from __future__ import annotations

import argparse
import copy
import json
import re
import sys
from pathlib import Path


SCHEMA = "minic-linux-cross-target-diagnostic-census-v2"
DISCOVERY_VERDICT = "MINIC_S_DISCOVERY_ONLY_NOT_GNU_AS_LINK_OR_QEMU"
TARGETS = frozenset(("x86_64", "aarch64", "loongarch64"))
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
# The producer permits either a >=200-owner common profile or a >=1000-owner
# broad profile.  The aggregate does not encode the profile, so use its lower
# valid bound here; the producer separately attests the exact frozen profile.
MIN_TARGET_OWNERS = 200


class CoverageError(ValueError):
    """The aggregate is missing evidence or contains an incomplete target."""


def certify(report: object) -> dict[str, int]:
    if not isinstance(report, dict):
        raise CoverageError("summary must be a JSON object")
    if report.get("schema") != SCHEMA:
        raise CoverageError("wrong census summary schema")
    if report.get("verdict") != DISCOVERY_VERDICT:
        raise CoverageError("wrong census stage or unsupported verdict")

    compiler_sha = report.get("compiler_sha256")
    if (not isinstance(compiler_sha, str)
            or not SHA256.fullmatch(compiler_sha)
            or compiler_sha == "0" * 64):
        raise CoverageError("missing or invalid common MiniC compiler SHA-256")

    targets = report.get("targets")
    if not isinstance(targets, dict) or set(targets) != TARGETS:
        got = sorted(targets) if isinstance(targets, dict) else type(targets).__name__
        raise CoverageError(f"incomplete three-ISA target set: {got}")

    # The producer checks all 21 original GNU shard manifests.  The source
    # gate then insists that *every* enumerated owner compiled successfully.
    totals: dict[str, int] = {}
    for arch in sorted(TARGETS):
        row = targets[arch]
        if not isinstance(row, dict):
            raise CoverageError(f"invalid {arch} coverage record")
        total = row.get("total")
        passed = row.get("pass_count")
        # bool is an int in Python: reject e.g. total=true as false evidence.
        if (type(total) is not int or type(passed) is not int
                or total < MIN_TARGET_OWNERS or passed < 0 or passed > total):
            raise CoverageError(f"invalid {arch} coverage counts total={total!r} passed={passed!r}")
        if passed != total:
            raise CoverageError(f"incomplete {arch} MiniC -S: {passed}/{total}")
        totals[arch] = total

    failures = report.get("diagnostic_groups")
    if not isinstance(failures, list) or failures:
        raise CoverageError("nonempty or invalid diagnostic_groups; TU failures remain")

    return totals


def self_test() -> None:
    valid = {
        "schema": SCHEMA,
        "verdict": DISCOVERY_VERDICT,
        "compiler_sha256": "a" * 64,
        "targets": {
            "x86_64": {"total": 2758, "pass_count": 2758},
            "aarch64": {"total": 4078, "pass_count": 4078},
            "loongarch64": {"total": 2723, "pass_count": 2723},
        },
        "diagnostic_groups": [],
        # These are diagnostic observations, NOT necessarily live blockers.
        "core_trace_frontiers": [{"observation_phase": "passing_tu_pre_prune_attempt"}],
    }
    assert certify(valid) == {"aarch64": 4078, "loongarch64": 2723, "x86_64": 2758}

    invalid: list[tuple[str, object]] = []

    def reject(name: str, mutate) -> None:
        test = copy.deepcopy(valid)
        mutate(test)
        invalid.append((name, test))

    reject("historic_aarch64_4076_of_4078", lambda x: x["targets"]["aarch64"].update(pass_count=4076))
    reject("missing_target", lambda x: x["targets"].pop("aarch64"))
    reject("extra_target", lambda x: x["targets"].update(riscv64={"total": 2064, "pass_count": 2064}))
    reject("invalid_sha", lambda x: x.update(compiler_sha256="abc"))
    reject("zero_sha", lambda x: x.update(compiler_sha256="0" * 64))
    reject("missing_sha", lambda x: x.pop("compiler_sha256"))
    reject("diagnostic_remaining", lambda x: x.update(diagnostic_groups=[{"diagnostic_signature": "failed"}]))
    reject("diagnostic_wrong_type", lambda x: x.update(diagnostic_groups=None))
    reject("zero_total", lambda x: x["targets"]["x86_64"].update(total=0, pass_count=0))
    reject("implausibly_small_total", lambda x: x["targets"]["x86_64"].update(total=1, pass_count=1))
    reject("boolean_total", lambda x: x["targets"]["x86_64"].update(total=True, pass_count=True))
    reject("over_count", lambda x: x["targets"]["loongarch64"].update(pass_count=2724))
    reject("string_count", lambda x: x["targets"]["loongarch64"].update(pass_count="2723"))
    reject("missing_count", lambda x: x["targets"]["loongarch64"].pop("pass_count"))
    reject("wrong_schema", lambda x: x.update(schema="minic-linux-old-census"))
    reject("wrong_verdict", lambda x: x.update(verdict="LINUX_QEMU_PASS"))
    invalid.append(("nonobject", []))
    for name, sample in invalid:
        try:
            certify(sample)
        except CoverageError:
            continue
        raise AssertionError(f"strict gate incorrectly accepted {name}")
    print(f"MINIC_S_FULL_COVERAGE_SELFTEST=PASS negative_cases={len(invalid)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--summary", type=Path, help="21-shard census-summary JSON")
    group.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            return 0
        with args.summary.open(encoding="utf-8") as stream:
            result = json.load(stream)
        counts = certify(result)
    except (OSError, ValueError, AssertionError) as exc:
        print(f"MINIC_S_FULL_COVERAGE_FAIL reason={exc}", file=sys.stderr)
        return 2
    print("MINIC_S_FULL_COVERAGE_PASS "
          + " ".join(f"{arch}={counts[arch]}/{counts[arch]}" for arch in sorted(TARGETS))
          + " AS_LINK_QEMU=NOT_RUN")
    return 0


if __name__ == "__main__":
    sys.exit(main())
