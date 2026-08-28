#!/usr/bin/env python3
"""Summarize first-error source-frontier progress for a Linux replay batch."""

from __future__ import annotations

from argparse import ArgumentParser
from collections import Counter
import json
import math
from pathlib import Path
import statistics
from typing import Any


def parse_args():
    parser = ArgumentParser()
    parser.add_argument("batch_results", type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--text-out", type=Path)
    parser.add_argument("--expect-selected", type=int, default=0)
    return parser.parse_args()


def percentile(sorted_values: list[float], fraction: float) -> float:
    if not sorted_values:
        raise ValueError("percentile requires at least one value")
    if len(sorted_values) == 1:
        return sorted_values[0]
    position = fraction * (len(sorted_values) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return sorted_values[lower]
    weight = position - lower
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight


def progress_bucket(value: float) -> str:
    if value >= 1.0:
        return "100%"
    if value < 0.10:
        return "0-10%"
    if value < 0.25:
        return "10-25%"
    if value < 0.50:
        return "25-50%"
    if value < 0.75:
        return "50-75%"
    if value < 0.90:
        return "75-90%"
    if value < 0.99:
        return "90-99%"
    return "99-100%"


def main() -> int:
    args = parse_args()
    rows: list[dict[str, Any]] = json.loads(args.batch_results.read_text())
    if args.expect_selected and len(rows) != args.expect_selected:
        raise SystemExit(f"expected {args.expect_selected} replay rows, got {len(rows)}")

    valid: list[tuple[dict[str, Any], float, int]] = []
    missing = 0
    for row in rows:
        total = row.get("total_lines")
        ratio = row.get("progress_ratio")
        if total is None or ratio is None or int(total) <= 0:
            missing += 1
            continue
        progress = min(1.0, max(0.0, float(ratio)))
        valid.append((row, progress, int(total)))

    if not valid:
        raise SystemExit("no rows contain total_lines + progress_ratio")

    values = sorted(progress for _, progress, _ in valid)
    total_lines = sum(total for _, _, total in valid)
    reached_lines = sum(progress * total for _, progress, total in valid)
    statuses = Counter(str(row.get("status", "")) for row in rows)
    buckets = Counter(progress_bucket(progress) for _, progress, _ in valid)

    summary = {
        "selected": len(rows),
        "measured": len(valid),
        "missing_metrics": missing,
        "pass": statuses["PASS"],
        "fail": statuses["FAIL"],
        "preprocess_missing": statuses["PREPROCESS_MISSING"],
        "tu_mean_progress": statistics.fmean(values),
        "tu_median_progress": statistics.median(values),
        "line_weighted_progress": reached_lines / total_lines,
        "total_source_lines": total_lines,
        "reached_source_line_equivalent": reached_lines,
        "p10": percentile(values, 0.10),
        "p25": percentile(values, 0.25),
        "p50": percentile(values, 0.50),
        "p75": percentile(values, 0.75),
        "p90": percentile(values, 0.90),
        "ge_50pct": sum(progress >= 0.50 for progress in values),
        "ge_75pct": sum(progress >= 0.75 for progress in values),
        "ge_90pct": sum(progress >= 0.90 for progress in values),
        "ge_99pct": sum(progress >= 0.99 for progress in values),
        "buckets": {
            name: buckets[name]
            for name in (
                "0-10%", "10-25%", "25-50%", "50-75%",
                "75-90%", "90-99%", "99-100%", "100%",
            )
        },
    }

    lines = [
        "CORE_FRONTIER_PROGRESS_SUMMARY",
        f"selected={summary['selected']}",
        f"measured={summary['measured']}",
        f"missing_metrics={summary['missing_metrics']}",
        f"pass={summary['pass']}",
        f"fail={summary['fail']}",
        f"tu_mean_progress={summary['tu_mean_progress'] * 100.0:.2f}%",
        f"tu_median_progress={summary['tu_median_progress'] * 100.0:.2f}%",
        f"line_weighted_progress={summary['line_weighted_progress'] * 100.0:.2f}%",
        f"p10={summary['p10'] * 100.0:.2f}%",
        f"p25={summary['p25'] * 100.0:.2f}%",
        f"p50={summary['p50'] * 100.0:.2f}%",
        f"p75={summary['p75'] * 100.0:.2f}%",
        f"p90={summary['p90'] * 100.0:.2f}%",
        f"ge_50pct={summary['ge_50pct']}",
        f"ge_75pct={summary['ge_75pct']}",
        f"ge_90pct={summary['ge_90pct']}",
        f"ge_99pct={summary['ge_99pct']}",
        f"total_source_lines={summary['total_source_lines']}",
        f"reached_source_line_equivalent={summary['reached_source_line_equivalent']:.1f}",
        "",
        "progress_buckets:",
    ]
    for name, count in summary["buckets"].items():
        lines.append(f"  {name}={count}")
    text = "\n".join(lines) + "\n"

    print(text, end="")
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    if args.text_out is not None:
        args.text_out.parent.mkdir(parents=True, exist_ok=True)
        args.text_out.write_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
