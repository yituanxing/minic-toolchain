#!/usr/bin/env python3
"""Same-runner O3 A/B on 20 SHA-verified ORIGINAL GNU Linux C owners.

Selection is predeclared: 20 lowest SHA256(object path) in shard 0. The
sampling key does not use compile timings, source size, or benchmark results.
The full seven-shard original GNU manifest and every input/.cmd SHA are verified
before selecting, via the existing production seven-shard verifier.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys


def module(name: str, filename: str):
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {filename}")
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def selection(rows: list[dict]) -> list[dict]:
    if len(rows) < 20:
        raise RuntimeError("fewer than 20 original GNU C owners")
    return sorted(rows, key=lambda row: (hashlib.sha256(
        row["object"].encode("utf-8")).hexdigest(), row["object"]))[:20]


def median_arm(proof: dict, arm: str) -> dict[str, float]:
    records = [row for row in proof["records"] if row["mode"] == arm]
    if len(records) != 4:
        raise RuntimeError("missing 4 ABBA samples")
    return {
        "cpu_ms": statistics.median(row["user_ms"] + row["sys_ms"] for row in records),
        "wall_ms": statistics.median(row["wall_ms"] for row in records),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-root", type=Path, required=True)
    parser.add_argument("--target", choices=("x86_64", "aarch64", "loongarch64"), required=True)
    parser.add_argument("--baseline-driver", type=Path, required=True)
    parser.add_argument("--candidate-driver", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists():
        raise RuntimeError("refuse to reuse old A/B output")
    out.mkdir(parents=True)

    seven = module("minic_seven_perf_input", "linux-three-isa-seven-runtime-v1.py")
    c = seven.candidate()
    input_root = args.input_root.resolve()
    rows, identity = seven.verify_input(input_root, args.target, 0, c)
    selected = selection(rows)
    assert len(selected) == len({row["object"] for row in selected}) == 20

    available = sorted(os.sched_getaffinity(0))
    os.sched_setaffinity(0, {available[0]})
    print(f"THREE_ISA_AB_SOURCE=PASS target={args.target} verified_shard_owners={len(rows)} "
          f"selected=20 pinned_cpu={available[0]} original_gnu=true", flush=True)
    meter = Path(__file__).with_name("linux-perf-pinned-single-paired-ab-v1.py")
    report = []
    for index, row in enumerate(selected):
        source = input_root / "frozen" / row["input"]
        expected = row["sha256"]
        if sha256(source) != expected:
            raise RuntimeError("original GNU input mismatch: " + row["object"])
        flags = [f"--target={args.target}-linux", *c.effective_minic_flags(row, args.target)]
        dst = out / f"owner-{index:02d}"
        command = [sys.executable, str(meter), "--input", str(source),
                   "--baseline-driver", str(args.baseline_driver.resolve()),
                   "--candidate-driver", str(args.candidate_driver.resolve()),
                   "--repeats", "4", "--out", str(dst),
                   *["--minic-arg=" + flag for flag in flags]]
        proc = subprocess.run(command, capture_output=True, text=True, timeout=920)
        if proc.returncode:
            raise RuntimeError(f"A/B FAIL target={args.target} owner={row['object']} "
                               f"rc={proc.returncode} stderr={proc.stderr[-1500:]}")
        proof = json.loads((dst / "paired-result.json").read_text())
        if (proof["input_sha256"] != expected or proof["minic_args"] != flags or
                proof["repeat_per_arm"] != 4 or proof["warmup_per_arm"] != 1 or
                not proof["assembly_identical"]):
            raise RuntimeError("A/B identity/equality FAIL owner=" + row["object"])
        before, after = median_arm(proof, "before"), median_arm(proof, "after")
        report.append({"owner": row["object"], "original_input_sha256": expected,
                       "original_cmd_sha256": row["cmd_sha256"], "minic_flags": flags,
                       "before": before, "after": after,
                       "assembly_sha256": proof["assembly_sha256"]})
        print(f"THREE_ISA_AB_OWNER=PASS target={args.target} n={index+1}/20 "
              f"owner={row['object']} base_cpu_ms={before['cpu_ms']:.3f} "
              f"cand_cpu_ms={after['cpu_ms']:.3f}", flush=True)

    totals = {arm: {metric: round(sum(row[arm][metric] for row in report), 3)
                    for metric in ("cpu_ms", "wall_ms")}
              for arm in ("before", "after")}
    result = {"schema": "minic-three-isa-original-gnu-shard0-hash20-paired-v1",
              "target": args.target, "source_release": "Linux 6.6.143",
              "original_gnu_input": True, "source_run": 42366441631,
              "source_identity": identity, "source_shard": 0,
              "selection": "20 lowest SHA256(original ELF object path), before timing",
              "baseline_commit": os.environ["BASE_SHA"],
              "candidate_commit": os.environ["CAND_SHA"],
              "same_runner_single_cpu": available[0], "repeats_per_arm": 4,
              "warmups_per_arm": 1, "assembly_identical": "20/20",
              "totals": totals, "speedup_cpu": round(totals["before"]["cpu_ms"] /
                                                  totals["after"]["cpu_ms"], 6),
              "speedup_wall": round(totals["before"]["wall_ms"] /
                                                   totals["after"]["wall_ms"], 6),
              "owners": report}
    (out / "summary.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"THREE_ISA_LINUX_AB=PASS target={args.target} original_gnu_owners=20/20 "
          f"cpu_base_ms={totals['before']['cpu_ms']:.3f} "
          f"cpu_cand_ms={totals['after']['cpu_ms']:.3f} "
          f"speedup_cpu={result['speedup_cpu']:.6f} "
          f"speedup_wall={result['speedup_wall']:.6f} asm_sha_equal=20/20", flush=True)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as f:
            f.write(f"### Original GNU Linux {args.target} SHA-frozen hash20\n\n"
                    f"{totals['before']['cpu_ms']:.3f} → "
                    f"{totals['after']['cpu_ms']:.3f} ms CPU medians sum; "
                    f"**{result['speedup_cpu']:.6f}x CPU throughput**; "
                    f"**{result['speedup_wall']:.6f}x Wall throughput**; "
                    "20/20 byte-identical assembly. Original Kbuild GNU -E inputs, "
                    "SHA/.cmd/ABI verified; same runner, one pinned CPU, 4 ABBA.\n")


if __name__ == "__main__":
    main()
