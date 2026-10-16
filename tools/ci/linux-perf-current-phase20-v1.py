#!/usr/bin/env python3
"""Run the EXISTING phase tracer on 20 SHA-frozen Linux inputs.

This only selects authentic previously benchmarked translation units, calls
linux-perf-phase-trace-v1.py, and aggregates the nonoverlapping phase times.
It does not touch production compiler code or claim instrumented times are
normal-speed CPU results. The traced compiler must emit byte-identical .s.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys


STAGES = ("parse", "normalize", "core-set", "core-validate", "layout", "codegen")
SPECIALIZATION = ("spec-integer", "spec-symbolic", "spec-boolean", "spec-finalize")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as inp:
        for chunk in iter(lambda: inp.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True, type=Path)
    ap.add_argument("--selection", required=True, type=Path)
    ap.add_argument("--baseline", required=True, type=Path)
    ap.add_argument("--traced", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    for p in (args.corpus, args.selection, args.baseline, args.traced):
        if not p.exists():
            raise RuntimeError(f"required input absent: {p}")

    manifest = args.corpus / "selected-tus.txt"
    selected = {}
    for line in manifest.read_text().splitlines():
        fields = line.split("\t", 3)
        if len(fields) != 4:
            raise RuntimeError("invalid frozen selected-tus.txt")
        index, owner, rel_i, source = fields
        if owner in selected:
            raise RuntimeError(f"duplicate corpus owner {owner}")
        path = (args.corpus / "kbuild" / rel_i).resolve()
        if not path.is_relative_to((args.corpus / "kbuild").resolve()):
            raise RuntimeError("corpus input traversal")
        selected[owner] = (int(index), path, rel_i)
    if len(selected) != 500:
        raise RuntimeError(f"expected 500 authenticated corpus TUs, got {len(selected)}")

    cases = []
    for row in args.selection.read_text().splitlines():
        if not row or row.lstrip().startswith("#"):
            continue
        cols = row.split("\t")
        if len(cols) != 3 or cols[0] not in ("hot", "middle", "light"):
            raise RuntimeError(f"bad sample selection: {row}")
        group, owner, prior = cols
        if owner not in selected:
            raise RuntimeError(f"selected owner outside frozen 500: {owner}")
        cases.append((group, owner, float(prior), *selected[owner]))
    if (len(cases) != 20 or len({r[1] for r in cases}) != 20 or
            [sum(r[0] == group for r in cases)
             for group in ("hot", "middle", "light")] != [10, 5, 5]):
        raise RuntimeError("stratified 20-owner sample is not exact")

    args.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for group, owner, prior_ms, index, path, rel in cases:
        if not path.is_file() or not path.stat().st_size:
            raise RuntimeError(f"missing immutable .i {path}")
        tag = f"{index:04d}"
        folder = args.out / tag
        folder.mkdir()
        proc = subprocess.run(
            [sys.executable, "tools/ci/linux-perf-phase-trace-v1.py",
             "--input", str(path),
             "--baseline", str(args.baseline),
             "--candidate", str(args.traced),
             "--expect-specialization", "--out", str(folder)],
            capture_output=True, text=True, errors="replace", timeout=390)
        (folder / "trace.log").write_text(proc.stdout + "\n" + proc.stderr)
        if proc.returncode:
            raise RuntimeError(f"phase trace failed owner={owner}: "
                               + (proc.stdout + proc.stderr)[-1800:])
        report = json.loads((folder / "phases.json").read_text())
        old, new = report["results"]["before"], report["results"]["after"]
        if new["assembly_sha"] != old["assembly_sha"]:
            raise RuntimeError(f"phase tracing changed assembly for {owner}")
        if not set(STAGES) <= set(new["stage_ms"]):
            raise RuntimeError(f"missing stage trace {owner}")
        spec = new["specialization_ms"]
        if not set(SPECIALIZATION) <= set(spec):
            raise RuntimeError(f"missing specialization trace {owner}")
        gap = new["interstage_gap_ms"]["normalize-to-core-set"]
        spec_sum = sum(spec[s]["ms"] for s in SPECIALIZATION)
        if spec_sum > gap + 10.0:
            raise RuntimeError(f"overlapping specialization attribution {owner}")
        phases = {name: new["stage_ms"][name] for name in STAGES}
        for name in SPECIALIZATION:
            phases[name] = spec[name]["ms"]
        phases["other_normalize_to_core_ms"] = max(0.0, gap-spec_sum)
        measured = sum(new["stage_ms"].values())
        measured += sum(new["interstage_gap_ms"].values())
        phases["other_phase_boundary_ms"] = max(
            0.0, sum(new["interstage_gap_ms"].values()) - gap)
        phases["outside_traced_boundary_ms"] = max(
            0.0, new["wall_ms"] - measured)
        rows.append({
            "group": group, "object": owner, "input": rel,
            "index": index, "historical_candidate_wall_ms": prior_ms,
            "input_sha256": sha256(path), "assembly_sha256": new["assembly_sha"],
            "baseline_wall_ms": old["wall_ms"], "traced_wall_ms": new["wall_ms"],
            "phase_ms_exclusive": phases,
            "specialization_calls": {k: spec[k]["calls"] for k in SPECIALIZATION},
            "stage_function_counts": new["function_counts"]})
        print(f"MINIC_PHASE20_PROGRESS checked={len(rows)}/20 owner={owner} "
              f"group={group} traced_ms={new['wall_ms']:.3f} "
              f"spec_integer_ms={spec['spec-integer']['ms']:.3f} "
              f"spec_symbolic_ms={spec['spec-symbolic']['ms']:.3f}", flush=True)

    totals = {key: round(sum(row["phase_ms_exclusive"][key] for row in rows), 3)
              for key in rows[0]["phase_ms_exclusive"]}
    traced_total = sum(row["traced_wall_ms"] for row in rows)
    summary = {
        "schema": "minic-current-linux-phase20-v1",
        "scope": "20 stratified frozen Linux .i, diagnostic wall time only",
        "exact_corpus_count": 500,
        "selected_groups": {"hot": 10, "middle": 5, "light": 5},
        "selection_sha256": sha256(args.selection),
        "baseline_cc_sha256": sha256(args.baseline.parent / "minic-cc"),
        "traced_cc_sha256": sha256(args.traced.parent / "minic-cc"),
        "all_assemblies_identical": True,
        "traced_total_ms": round(traced_total, 3),
        "phase_totals_ms_nonoverlap": totals, "owners": rows,
        "warning": "diagnostic times are NOT uninstrumented throughput or CPU-time; profiling further required"
    }
    (args.out / "phase20.json").write_text(json.dumps(summary, indent=2) + "\n")
    with (args.out / "phase20.csv").open("w", newline="") as f:
        fields = ["group", "object", "historical_candidate_wall_ms", "traced_wall_ms"] + list(totals)
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for item in rows:
            writer.writerow({**{k: item[k] for k in fields[:4]},
                             **item["phase_ms_exclusive"]})
    print(f"MINIC_PHASE20=PASS files={len(rows)} assemblies_identical=20/20 "
          f"traced_total_ms={traced_total:.3f} groups=10,5,5")
    for name, duration in sorted(totals.items(), key=lambda x: -x[1]):
        print(f"MINIC_PHASE20_HOTSPOT phase={name} ms={duration:.3f} "
              f"share_percent={100.0*duration/traced_total:.2f}")
    print("MINIC_PHASE20_SCOPE=DIAGNOSTIC_ONLY not_normal_speed not_QEMU_P1")


if __name__ == "__main__":
    main()
