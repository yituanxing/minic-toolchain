#!/usr/bin/env python3
"""Replay only x86 P2's 36 previously failed original GNU C owners.

This is a targeted compiler regression audit, NEVER a substitute for the
2939-owner full census or real 18/18 GNU+MiniC QEMU guest differential.
Original GNU -E inputs and .o.cmd machine flags stay SHA-frozen and untouched.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import sys


HERE = Path(__file__).resolve().parent
P2 = HERE / "linux-three-isa-p2-compile-census-v1.py"
EXPECTED_BASELINE_SHA = "fe8a910172209f310a2e1f5324df4dc24c735aafc8e87ff50e94d0ee90427b5e"


def p2_module():
    spec = importlib.util.spec_from_file_location("p2_x86_baseline", P2)
    if spec is None or spec.loader is None:
        raise ValueError("missing full P2 original GNU source audit")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--minic", type=Path, required=True)
    parser.add_argument("--expected-driver-sha", required=True)
    parser.add_argument("--expected-cc-sha", required=True)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if not 1 <= args.workers <= 8 or not 1 <= args.timeout <= 900:
            raise ValueError("invalid targeted regression build limits")
        if args.output.exists():
            raise ValueError("refuse to overwrite any previous regression audit")
        if shutil.disk_usage(args.work).free < 8 * 1024**3:
            raise ValueError("8GiB free disk floor reached: preserve original evidence")

        p2 = p2_module()
        C = p2.C
        if p2.sha(C.required(args.baseline)) != EXPECTED_BASELINE_SHA:
            raise ValueError("the original 36-owner failure baseline SHA changed")
        baseline = p2.read_json(args.baseline)
        census_args = argparse.Namespace(target="x86_64", work=args.work,
                                         evidence=args.evidence)
        owners, identity = p2.census_inputs(census_args)
        if (baseline.get("schema") != "minic-p2-direct-seven-census-v1" or
                baseline.get("verdict") != "BLOCKED_NO_GNU_C_FALLBACK" or
                baseline.get("owners") != 2939 or
                baseline.get("mini_c_gnu_as_pass") != 2903 or
                baseline.get("failed") != 36 or
                baseline.get("gnu_c_fallback") != 0 or
                baseline.get("runtime_certified") is not False or
                any(baseline.get(k) != value for k, value in identity.items())):
            raise ValueError("original x86 full census was not the pinned 2903/2939 baseline")

        failed = baseline.get("failures", [])
        failed_names = [entry.get("object") for entry in failed]
        if (len(failed_names) != 36 or len(set(failed_names)) != 36 or
                any(entry.get("status") != "MINIC_S_FAIL" for entry in failed)):
            raise ValueError("baseline 36 x86 C-owner failures changed")
        by_name = {row["object"]: row for row in owners}
        if any(name not in by_name for name in failed_names):
            raise ValueError("failed owner is absent from immutable GNU P2 C-owner census")
        selected = [by_name[name] for name in failed_names]
        for row in selected:
            inp = C.required(args.work / "frozen" / row["input"])
            name = Path(row["object"])
            command = C.required(args.work / "build" / name.parent /
                                 ("." + name.name + ".cmd"))
            if p2.sha(inp) != row["sha256"] or p2.sha(command) != row["cmd_sha256"]:
                raise ValueError("GNU input or .o.cmd mutated: " + row["object"])

        compiled_pair = C.compiler_pair(args.minic)
        if (compiled_pair["compiler_driver_sha256"] != args.expected_driver_sha or
                compiled_pair["compiler_cc_sha256"] != args.expected_cc_sha):
            raise ValueError("MiniC driver/CC does not match requested latest binary SHA pair")
        args.output.mkdir(parents=True)
        results = C.make_objects(selected, args.work, args.minic, args.output,
                                 identity, workers=args.workers, timeout=args.timeout)
        C.compiler_pair(args.minic, expected=compiled_pair)
        if len(results) != 36:
            raise ValueError("truncated targeted MiniC C-owner result")
        failure_rows = []
        for original, row in zip(selected, results):
            if row["object"] != original["object"] or row["input_sha256"] != original["sha256"]:
                raise ValueError("targeted replay substituted a GNU C-owner")
            if row["status"] == "MINIC_S_GNU_AS_PASS":
                obj = C.required(args.output / "staged" / row["object"])
                asm = C.required(args.output / "staged" /
                                 Path(row["object"]).with_suffix(".s"))
                if row["asm_sha256"] != p2.sha(asm) or row["obj_sha256"] != p2.sha(obj):
                    raise ValueError("targeted MiniC assembly/object SHA mismatch")
                C.elf_machine(obj, identity["machine"])
            else:
                C.required(args.output / "staged" /
                           Path(row["object"]).with_suffix(".tool-stderr.txt"))
                failure_rows.append({"object": row["object"], "status": row["status"],
                                     "detail": row.get("detail", "")})

        (args.output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
        receipt = {"schema": "minic-p2-x86-old-fail36-focused-replay-v1",
                   **identity, **compiled_pair,
                   "old_full_census_sha256": EXPECTED_BASELINE_SHA,
                   "old_full_census_pass": 2903, "old_full_census_failed": 36,
                   "old_full_census_owners": 2939,
                   "targeted_original_failures": 36,
                   "targeted_pass": 36 - len(failure_rows),
                   "targeted_fail": len(failure_rows),
                   "targeted_failures": failure_rows,
                   "results_sha256": p2.sha(args.output / "results.json"),
                   "verdict": ("TARGETED36_MINIC_S_GNU_AS_PASS" if not failure_rows else
                               "BLOCKED_TARGETED36_NO_GNU_C_FALLBACK"),
                   "full_2939_reaudited": False,
                   "gnu_p2_runtime_certified": False,
                   "gnu_c_fallback": 0}
        (args.output / "receipt.json").write_text(
            json.dumps(receipt, indent=2, sort_keys=True) + "\n")
        print("X86_P2_TARGETED36=%s MiniC_GNU_AS=%d/36 failures=%d "
              "full_2939_reaudited=NO GNU_P2_RUNTIME=NOT_CERTIFIED" %
              (receipt["verdict"], receipt["targeted_pass"], len(failure_rows)))
        return 0 if not failure_rows else 1
    except (OSError, KeyError, ValueError) as exc:
        print("X86_P2_TARGETED36=FAIL reason=" + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
