#!/usr/bin/env python3
"""FULL, original-GNU Linux 6.6.143 seven-shard same-runner MiniC A/B.

No source sampling: every owner from the immutable, checked GNU shard is run.
For each owner, one untimed warmup per compiler precedes two alternating
measurements per compiler (A/B, B/A). Output hashes must agree on every run.
GNU -E, original .cmd hashes, and original per-owner ABI flags are preserved.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import resource
import statistics
import subprocess
import sys
import time


BASE_SHA = "e13fc7c77e60cf80f6ad699035998b5aada3a236"
CAND_SHA = "aa186412c4165eaa7a4ccd1a406b8fc7918e3326"
SOURCE_RUN = 42366441631
COUNTS = {"x86_64": 2758, "aarch64": 4108, "loongarch64": 2723}
TARGETS = tuple(COUNTS)
SCHEMA = "minic-three-isa-original-gnu-complete-seven-shard-paired-v1"


def module(name: str, filename: str):
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load source identity module: {filename}")
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


def file_sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def output_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x") as stream:
        json.dump(data, stream, indent=2, sort_keys=True)
        stream.write("\n")


def compile_once(driver: Path, source: Path, asm: Path, flags: list[str]) -> dict:
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.perf_counter_ns()
    proc = subprocess.run([str(driver), *flags, "-S", str(source), "-o", str(asm)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=120, text=True, errors="replace", check=False)
    wall_ms = (time.perf_counter_ns() - started) / 1e6
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if proc.returncode or not asm.is_file() or asm.stat().st_size == 0:
        raise RuntimeError(f"compiler failed rc={proc.returncode} stderr={proc.stderr[-1400:]}")
    return {"wall_ms": round(wall_ms, 3),
            "user_ms": round(1000 * (after.ru_utime - before.ru_utime), 3),
            "sys_ms": round(1000 * (after.ru_stime - before.ru_stime), 3),
            "sha256": file_sha(asm), "assembly_bytes": asm.stat().st_size}


def median(records: list[dict], arm: str) -> dict[str, float]:
    values = [r for r in records if r["arm"] == arm]
    if len(values) != 2:
        raise RuntimeError("missing paired arm measurement")
    return {"cpu_ms": round(statistics.median(r["user_ms"] + r["sys_ms"]
                                               for r in values), 3),
            "wall_ms": round(statistics.median(r["wall_ms"] for r in values), 3)}


def measure(args: argparse.Namespace) -> None:
    if args.output.exists():
        raise RuntimeError("refuse to reuse old A/B measurement directory")
    args.output.mkdir(parents=True)
    seven = module("strict_seven", "linux-three-isa-seven-runtime-v1.py")
    candidate = seven.candidate()
    target = args.target
    shard = args.shard
    rows, identity = seven.verify_input(args.input_root.resolve(), target, shard, candidate)
    if identity["total_c_owners"] != COUNTS[target]:
        raise RuntimeError(f"unexpected GNU census {identity['total_c_owners']} for {target}")
    if len(rows) != len(range(shard, COUNTS[target], 7)):
        raise RuntimeError("incomplete original seven-shard input")
    drivers = {"before": args.baseline_driver.resolve(),
               "after": args.candidate_driver.resolve()}
    compiler_sha = {name: file_sha(driver.parent / "minic-cc")
                    for name, driver in drivers.items()}
    if compiler_sha["before"] == compiler_sha["after"]:
        raise RuntimeError("A/B compiler binaries are identical")
    if any(not path.is_file() for path in drivers.values()):
        raise RuntimeError("missing compiler driver")
    cpus = sorted(os.sched_getaffinity(0))
    if not cpus:
        raise RuntimeError("cannot select pinned CPU")
    os.sched_setaffinity(0, {cpus[0]})
    print(f"THREE_ISA_FULL_AB_SOURCE=PASS target={target} shard={shard} "
          f"owners={len(rows)} of={COUNTS[target]} single_cpu={cpus[0]} "
          "original_gnu=true input_cmd_config_sha_verified=true", flush=True)
    tmp = args.output / "work"
    tmp.mkdir()
    report: list[dict] = []
    started = time.monotonic()
    for index, row in enumerate(rows):
        source = args.input_root / "frozen" / row["input"]
        if file_sha(source) != row["sha256"]:
            raise RuntimeError("original GNU source SHA changed: " + row["object"])
        flags = [f"--target={target}-linux", *candidate.effective_minic_flags(row, target)]
        expected_sha = None
        records = []
        for arm in ("before", "after"):
            compiled = compile_once(drivers[arm], source, tmp / f"{arm}.s", flags)
            expected_sha = expected_sha or compiled["sha256"]
            if expected_sha != compiled["sha256"]:
                raise RuntimeError(f"warmup ASM diverged target={target} "
                                   f"shard={shard} owner={row['object']} arm={arm}")
        for i, arms in enumerate((("before", "after"), ("after", "before"))):
            for arm in arms:
                compiled = compile_once(drivers[arm], source, tmp / f"{arm}.s", flags)
                if expected_sha != compiled["sha256"]:
                    raise RuntimeError(f"ASM SHA mismatched target={target} shard={shard} "
                                       f"owner={row['object']} arm={arm} repeat={i}")
                records.append({"repeat": i, "arm": arm, **compiled})
        before, after = median(records, "before"), median(records, "after")
        report.append({"owner": row["object"], "input_sha256": row["sha256"],
                       "cmd_sha256": row["cmd_sha256"], "minic_args": flags,
                       "assembly_sha256": expected_sha, "before": before,
                       "after": after, "records": records})
        if (index + 1) % 25 == 0 or index + 1 == len(rows):
            print(f"THREE_ISA_FULL_AB_PROGRESS target={target} shard={shard} "
                  f"owners={index+1}/{len(rows)} elapsed_s={time.monotonic()-started:.1f}",
                  flush=True)
    if len(report) != len(rows) or len({r["owner"] for r in report}) != len(rows):
        raise RuntimeError("missing or duplicated owners")
    totals = {arm: {metric: round(sum(r[arm][metric] for r in report), 3)
                    for metric in ("cpu_ms", "wall_ms")} for arm in ("before", "after")}
    measured_elapsed_ms = round((time.monotonic() - started) * 1000, 3)
    proof = {"schema": SCHEMA, "source_run": SOURCE_RUN, "source_release": "Linux 6.6.143",
             "original_gnu_input": True, "source_identity": identity,
             "target": target, "shard": shard, "total_c_owners": COUNTS[target],
             "measured_c_owners": len(report), "baseline_commit": BASE_SHA,
             "candidate_commit": CAND_SHA, "compiler_cc_sha256": compiler_sha,
             "single_pinned_cpu": cpus[0], "warmup_per_arm": 1,
             "repeats_per_arm": 2, "measurement_order": "A,B,B,A",
             "measurement_loop_elapsed_ms": measured_elapsed_ms,
             "assembly_identical": f"{len(report)}/{len(report)}",
             "totals": totals,
             "speedup_cpu": round(totals["before"]["cpu_ms"] / totals["after"]["cpu_ms"], 6),
             "speedup_wall": round(totals["before"]["wall_ms"] / totals["after"]["wall_ms"], 6),
             "owners": report}
    output_json(args.output / "summary.json", proof)
    for arm in ("before", "after"):
        (tmp / f"{arm}.s").unlink(missing_ok=True)
    tmp.rmdir()
    print(f"THREE_ISA_FULL_AB=PASS target={target} shard={shard} "
          f"owners={len(report)}/{len(rows)} "
          f"base_cpu_ms={totals['before']['cpu_ms']:.3f} "
          f"cand_cpu_ms={totals['after']['cpu_ms']:.3f} "
          f"speedup_cpu={proof['speedup_cpu']:.6f} "
          f"asm_sha_equal={len(report)}/{len(report)}", flush=True)


def aggregate(folder: Path, output: Path) -> None:
    if output.exists():
        raise RuntimeError("refuse to overwrite aggregate proof")
    all_targets = {}
    for target in TARGETS:
        shards = []
        owners = set()
        frozen_sha = set()
        config_sha = set()
        compiler_shas = {"before": set(), "after": set()}
        for shard in range(7):
            path = folder / f"minic-three-isa-full-gnu-ab-{target}-{shard}" / "owners" / "summary.json"
            data = json.loads(path.read_text())
            if (data["schema"] != SCHEMA or data["target"] != target or
                    data["shard"] != shard or data["baseline_commit"] != BASE_SHA or
                    data["candidate_commit"] != CAND_SHA or
                    data["source_run"] != SOURCE_RUN or
                    data["total_c_owners"] != COUNTS[target] or
                    data["measured_c_owners"] != len(range(shard, COUNTS[target], 7)) or
                    data["assembly_identical"] != f"{len(data['owners'])}/{len(data['owners'])}" or
                    data["warmup_per_arm"] != 1 or data["repeats_per_arm"] != 2 or
                    data["measurement_order"] != "A,B,B,A"):
                raise RuntimeError(f"invalid full proof target={target} shard={shard}")
            frozen_sha.add(data["source_identity"]["frozen_manifest_sha256"])
            config_sha.add(data["source_identity"]["original_config_sha256"])
            for arm in compiler_shas:
                compiler_shas[arm].add(data["compiler_cc_sha256"][arm])
            if len(data["owners"]) != data["measured_c_owners"]:
                raise RuntimeError("truncated owner results")
            for item in data["owners"]:
                if item["owner"] in owners:
                    raise RuntimeError("duplicated owner across shards: " + item["owner"])
                if len(item["records"]) != 4 or any(
                        r["sha256"] != item["assembly_sha256"] for r in item["records"]):
                    raise RuntimeError("unverified full owner ASM")
                owners.add(item["owner"])
            shards.append({"shard": shard, "count": data["measured_c_owners"],
                           "totals": data["totals"],
                           "measurement_loop_elapsed_ms": data["measurement_loop_elapsed_ms"],
                           "speedup_cpu": data["speedup_cpu"],
                           "speedup_wall": data["speedup_wall"]})
        if (len(owners) != COUNTS[target] or len(frozen_sha) != 1 or
                len(config_sha) != 1 or
                any(len(s) != 1 for s in compiler_shas.values())):
            raise RuntimeError(f"incomplete census/identity target={target}: {len(owners)}")
        totals = {arm: {metric: round(sum(s["totals"][arm][metric] for s in shards), 3)
                        for metric in ("cpu_ms", "wall_ms")}
                  for arm in ("before", "after")}
        max_wall = {arm: round(max(s["totals"][arm]["wall_ms"] for s in shards), 3)
                    for arm in ("before", "after")}
        real_measure_max_wall = round(max(s["measurement_loop_elapsed_ms"] for s in shards), 3)
        all_targets[target] = {"owners": len(owners), "assembly_identical": f"{len(owners)}/{len(owners)}",
                               "source_manifest_sha256": next(iter(frozen_sha)),
                               "source_config_sha256": next(iter(config_sha)),
                               "compiler_cc_sha256": {arm: next(iter(s))
                                                       for arm, s in compiler_shas.items()},
                               "totals": totals, "seven_parallel_max_wall_ms": max_wall,
                               "seven_shard_max_measured_loop_elapsed_ms": real_measure_max_wall,
                               "speedup_cpu": round(totals["before"]["cpu_ms"] /
                                                    totals["after"]["cpu_ms"], 6),
                               "speedup_wall": round(totals["before"]["wall_ms"] /
                                                     totals["after"]["wall_ms"], 6),
                               "speedup_seven_parallel_wall": round(max_wall["before"] /
                                                                     max_wall["after"], 6),
                               "shards": shards}
    proof = {"schema": "minic-three-isa-original-gnu-full-aggregate-v1",
             "source_run": SOURCE_RUN, "baseline_commit": BASE_SHA,
             "candidate_commit": CAND_SHA, "targets": all_targets,
             "full_total_owners": sum(COUNTS.values()),
             "full_assembly_identical": f"{sum(COUNTS.values())}/{sum(COUNTS.values())}"}
    output_json(output, proof)
    print(f"THREE_ISA_FULL_LINUX_AGGREGATE=PASS owners={sum(COUNTS.values())}/"
          f"{sum(COUNTS.values())} original_gnu_asm_sha_equal=true", flush=True)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    lines = ["### FULL Original GNU Linux 6.6.143 MiniC A/B (not 20-owner sample)", "",
             "Each of the 7 original GNU shards/ISA is SHA/ABI validated and measured on "
             "a single pinned CPU, same runner, 1 warmup + 2 ABBA repetitions per arm.", "",
             "| Target | C owners verified | Baseline CPU ms | Optimized CPU ms | CPU speedup | "
             "Baseline wall ms (sum) | Optimized wall ms (sum) | Baseline 7-shard max ms | "
             "Optimized 7-shard max ms | Full actual 7-shard max measurement loop ms |",
             "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for target, proof in all_targets.items():
        b, a = proof["totals"]["before"], proof["totals"]["after"]
        wall = proof["seven_parallel_max_wall_ms"]
        lines.append(f"| {target} | {proof['owners']}/{COUNTS[target]} | "
                     f"{b['cpu_ms']:.3f} | {a['cpu_ms']:.3f} | {proof['speedup_cpu']:.6f}x | "
                     f"{b['wall_ms']:.3f} | {a['wall_ms']:.3f} | "
                     f"{wall['before']:.3f} | {wall['after']:.3f} | "
                     f"{proof['seven_shard_max_measured_loop_elapsed_ms']:.3f} |")
    if summary:
        with open(summary, "a") as stream:
            stream.write("\n".join(lines) + "\n")
    print("\n".join(lines), flush=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="mode", required=True)
    p = sub.add_parser("measure")
    p.add_argument("--input-root", type=Path, required=True)
    p.add_argument("--target", choices=TARGETS, required=True)
    p.add_argument("--shard", type=int, choices=range(7), required=True)
    p.add_argument("--baseline-driver", type=Path, required=True)
    p.add_argument("--candidate-driver", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    q = sub.add_parser("aggregate")
    q.add_argument("--input", type=Path, required=True)
    q.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.mode == "measure":
        measure(args)
    else:
        aggregate(args.input, args.output)


if __name__ == "__main__":
    main()
