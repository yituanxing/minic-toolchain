#!/usr/bin/env python3
from __future__ import annotations
import argparse, csv, json, re, statistics
from collections import defaultdict
from pathlib import Path

TRACE_RE = re.compile(
    r"MINIC_BOOTSTRAP_TRACE stage=(\S+) state=(\S+) functions=(\d+) "
    r"mono_ns=(\d+) cpu_ns=(\d+) input=(.*)$"
)

PHASES = ["parse", "normalize", "core-set", "core-validate", "layout", "codegen"]

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--work", required=True, type=Path)
    p.add_argument("--out", required=True, type=Path)
    return p.parse_args()

def ns_ms(v: int) -> float:
    return v / 1_000_000.0

def main():
    args = parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    results = json.loads((args.work / "batch-results.json").read_text())
    by_input = {str(r["input"]): r for r in results}
    rows = []
    totals_cpu = defaultdict(float)
    totals_wall = defaultdict(float)

    for stderr in sorted((args.work / "minic-stderr").rglob("*.stderr")):
        text = stderr.read_text(errors="replace")
        events = []
        for line in text.splitlines():
            m = TRACE_RE.search(line)
            if not m:
                continue
            events.append({
                "stage": m.group(1), "state": m.group(2),
                "functions": int(m.group(3)), "mono_ns": int(m.group(4)),
                "cpu_ns": int(m.group(5)), "input": m.group(6),
            })
        if not events:
            continue
        input_path = events[0]["input"]
        rel = None
        for key in by_input:
            if input_path.endswith(key):
                rel = key
                break
        if rel is None:
            rel = input_path
        result = by_input.get(rel, {})
        ev = {(e["stage"], e["state"]): e for e in events}
        row = {
            "input": rel,
            "index": result.get("index", ""),
            "process_seconds": float(result.get("seconds", 0.0)),
            "functions": max((e["functions"] for e in events), default=0),
        }

        def interval(label, a, b):
            if a is None or b is None or b["mono_ns"] < a["mono_ns"] or b["cpu_ns"] < a["cpu_ns"]:
                row[label + "_wall_ms"] = ""
                row[label + "_cpu_ms"] = ""
                return
            wall = ns_ms(b["mono_ns"] - a["mono_ns"])
            cpu = ns_ms(b["cpu_ns"] - a["cpu_ns"])
            row[label + "_wall_ms"] = wall
            row[label + "_cpu_ms"] = cpu
            totals_wall[label] += wall
            totals_cpu[label] += cpu

        for phase in PHASES:
            begin = ev.get((phase, "begin"))
            end = ev.get((phase, "end-ok")) or ev.get((phase, "end-fail"))
            interval(phase.replace("-", "_"), begin, end)

        parse_end = ev.get(("parse", "end-ok"))
        normalize_begin = ev.get(("normalize", "begin"))
        normalize_end = ev.get(("normalize", "end-ok"))
        core_begin = ev.get(("core-set", "begin"))
        core_end = ev.get(("core-set", "end-ok"))
        validate_begin = ev.get(("core-validate", "begin"))
        validate_end = ev.get(("core-validate", "end-ok"))
        layout_begin = ev.get(("layout", "begin"))
        layout_end = ev.get(("layout", "end-ok"))
        codegen_begin = ev.get(("codegen", "begin"))
        interval("post_parse_verify_gap", parse_end, normalize_begin)
        interval("post_normalize_specialization_gap", normalize_end, core_begin)
        interval("pre_core_validate_gap", core_end, validate_begin)
        interval("pre_layout_gap", validate_end, layout_begin)
        interval("pre_codegen_gap", layout_end, codegen_begin)

        first = min(events, key=lambda e: e["mono_ns"])
        last = max(events, key=lambda e: e["mono_ns"])
        row["traced_wall_ms"] = ns_ms(last["mono_ns"] - first["mono_ns"])
        row["traced_cpu_ms"] = ns_ms(last["cpu_ns"] - first["cpu_ns"])
        rows.append(row)

    if not rows:
        raise SystemExit("no timestamped MINIC_BOOTSTRAP_TRACE events found")

    fields = ["index", "input", "process_seconds", "functions", "traced_wall_ms", "traced_cpu_ms"]
    labels = [
        "parse", "post_parse_verify_gap", "normalize",
        "post_normalize_specialization_gap", "core_set", "pre_core_validate_gap",
        "core_validate", "pre_layout_gap", "layout", "pre_codegen_gap", "codegen",
    ]
    for label in labels:
        fields += [label + "_wall_ms", label + "_cpu_ms"]

    with (args.out / "phase-profile.tsv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, delimiter="\t", extrasaction="ignore")
        w.writeheader()
        for row in sorted(rows, key=lambda r: int(r["index"]) if str(r["index"]).isdigit() else 10**9):
            w.writerow(row)

    total_cpu = sum(totals_cpu.values())
    summary = {
        "tu_count": len(rows),
        "sum_process_seconds": sum(r["process_seconds"] for r in rows),
        "sum_traced_cpu_ms": sum(r["traced_cpu_ms"] for r in rows),
        "sum_traced_wall_ms": sum(r["traced_wall_ms"] for r in rows),
        "phases": {},
        "top_tus_by_process_seconds": [
            {"index": r["index"], "input": r["input"], "seconds": r["process_seconds"],
             "functions": r["functions"]}
            for r in sorted(rows, key=lambda r: r["process_seconds"], reverse=True)[:20]
        ],
    }
    for label in labels:
        cpu = totals_cpu[label]
        wall = totals_wall[label]
        summary["phases"][label] = {
            "cpu_ms": cpu,
            "wall_ms": wall,
            "cpu_pct_of_measured_phases": (cpu / total_cpu * 100.0) if total_cpu else 0.0,
        }
    (args.out / "phase-summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True))

    print(f"PROFILE_TUS={len(rows)}")
    print(f"PROFILE_SUM_PROCESS_SECONDS={summary['sum_process_seconds']:.3f}")
    print(f"PROFILE_SUM_TRACED_CPU_MS={summary['sum_traced_cpu_ms']:.3f}")
    for label, data in sorted(summary["phases"].items(),
                              key=lambda kv: kv[1]["cpu_ms"], reverse=True):
        print(f"PROFILE_PHASE {label} cpu_ms={data['cpu_ms']:.3f} "
              f"pct={data['cpu_pct_of_measured_phases']:.2f} wall_ms={data['wall_ms']:.3f}")
    print("PROFILE_TOP_TUS")
    for r in summary["top_tus_by_process_seconds"][:10]:
        print(f"  index={r['index']} seconds={r['seconds']:.3f} functions={r['functions']} input={r['input']}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
