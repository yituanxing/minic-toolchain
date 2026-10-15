#!/usr/bin/env python3
"""Offline, fail-closed seven-shard scheduling experiment on saved per-TU timings.

This emits a PLAN only. It never rewrites an existing validated shard producer
or cache identity; integration into production requires a separately certified
producer + merger contract and fresh native-object identities.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath

def plan(data, n=7):
    if n != 7 or not data:
        raise ValueError("only a nonempty seven-shard plan is supported")
    cap = (len(data) + n - 1) // n
    bins = [{"shard": i, "weight_ms": 0, "members": []} for i in range(n)]
    for name, ms, old in sorted(data, key=lambda x: (-x[1], x[0])):
        choices = [b for b in bins if len(b["members"]) < cap]
        if not choices:
            raise ValueError("capacity exhausted")
        chosen = min(choices, key=lambda b: (b["weight_ms"], len(b["members"]), b["shard"]))
        chosen["members"].append(name)
        chosen["weight_ms"] += ms
    if sum(len(b["members"]) for b in bins) != len(data):
        raise ValueError("incomplete result")
    return bins

def parse(rows, mode):
    required = ("minic_wall_ms",) if mode == "minic" else (
        "gnu_pp_wall_ms", "minipp_wall_ms", "minic_wall_ms", "minias_wall_ms")
    validated = []
    seen = set()
    for item in rows:
        name = item["object"]
        path = PurePosixPath(name)
        if (path.is_absolute() or ".." in path.parts or len(path.parts) < 2
            or not name.endswith(".o") or "\\" in name or name in seen):
            raise ValueError("invalid/duplicate object: " + name)
        seen.add(name)
        if item.get("status") != "PASS" or "shard" not in item:
            raise ValueError("uncertified or unsharded object: " + name)
        old = item["shard"]
        if not isinstance(old, int) or old < 0 or old >= 7:
            raise ValueError("old shard out of range: " + name)
        costs = [item[k] for k in required]
        if any(type(v) is not int or v < 0 for v in costs):
            raise ValueError("missing or invalid integer per-stage time: " + name)
        validated.append((name, sum(costs), old))
    return validated

def compare(rows, bins):
    old = [0]*7
    for _, ms, shard in rows:
        old[shard] += ms
    new = [b["weight_ms"] for b in bins]
    if sum(old) != sum(new):
        raise ValueError("planned and historical work totals differ")
    return {"historical_ms": old, "predicted_ms": new,
            "historical_critical_ms": max(old),
            "predicted_critical_ms": max(new),
            "estimated_critical_path_saving_pct": round(
                (max(old) - max(new)) * 100 / max(old), 3) if max(old) else 0,
            "scope": "offline deterministic forecast; not a measured CI speedup"}

def self_test():
    example = [("net/t%03d.o" % i, (i % 17 + 1)*43, i % 7)
               for i in range(2057)]
    a = plan(example)
    b = plan(example)
    assert a == b
    assert len(a) == 7
    assert sum(len(x["members"]) for x in a) == 2057
    assert set(x for p in a for x in p["members"]) == set(x[0] for x in example)
    assert max(len(x["members"]) for x in a) == 294
    assert sum(compare(example, a)["predicted_ms"]) == sum(x[1] for x in example)
    print("PERF_SHARD_PLAN_SELFTEST=PASS no_compile=1")

def main():
    cli = argparse.ArgumentParser()
    cli.add_argument("--metrics", type=Path)
    cli.add_argument("--manifest", type=Path)
    cli.add_argument("--mode", choices=["minic", "pipeline"], default="pipeline")
    cli.add_argument("--report", type=Path)
    cli.add_argument("--self-test", action="store_true")
    args = cli.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.metrics or not args.manifest or not args.report:
        cli.error("metrics, manifest and report are required outside self-test")
    metrics_bytes = args.metrics.read_bytes()
    data = json.loads(metrics_bytes)
    names = [l.strip() for l in args.manifest.read_text().splitlines()
             if l.strip() and not l.lstrip().startswith("#")]
    if len(names) != len(set(names)) or not names:
        raise ValueError("duplicate/empty C owner manifest")
    rows = parse(data, args.mode)
    if set(names) != set(name for name, _, _ in rows):
        raise ValueError("timing data does not match exact C owner manifest")
    assignments = plan(rows)
    stats = compare(rows, assignments)
    output = {"schema": "linux-perf-time-balanced-shards-v1",
              "mode": args.mode, "objects": len(rows), "shards": 7,
              "input_sha256": hashlib.sha256(metrics_bytes).hexdigest(),
              "manifest_sha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
              "statistics": stats,
              "plans": assignments}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(output, sort_keys=True, indent=2) + "\n")
    print("PERF_SHARD_PLAN=PASS objects=%s old_max_ms=%s forecast_max_ms=%s predicted_saving_pct=%s" %
          (len(rows), stats["historical_critical_ms"],
           stats["predicted_critical_ms"], stats["estimated_critical_path_saving_pct"]))
    print("PERF_SHARD_PLAN_EVIDENCE=FORECAST_ONLY native_objects_recompiled=0")

if __name__ == "__main__":
    main()
