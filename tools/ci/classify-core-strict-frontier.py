#!/usr/bin/env python3
"""Classify a frozen Linux Core-strict replay by blocker and trace feature family."""

from __future__ import annotations

from collections import defaultdict
from pathlib import Path
import argparse
import json
import re

UNSUPPORTED_RE = re.compile(r"^Core IR shadow does not yet support function '([^']+)'$")
TRACE_MARKER_RE = re.compile(r"^(CORE_[A-Z0-9_]+)\b")
TRACE_FIELD_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")
EXPRESSION_KIND_FIELDS = (
    "source_kind",
    "operand_kind",
    "condition_kind",
    "expression_kind",
    "target_kind",
    "callee_kind",
    "argument_kind",
    "value_kind",
)
VOLATILE_TRACE_FIELDS = {
    "function",
    "status",
    "span",
    "break_target",
    "block",
    "block_id",
    "value",
    "value_id",
    "object",
    "object_id",
    "local",
    "local_id",
    "statement",
    "statement_id",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("results", type=Path)
    parser.add_argument("--examples", type=int, default=5)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--reason-text-out", type=Path)
    parser.add_argument("--stderr-root", type=Path)
    parser.add_argument("--ast-header", type=Path, default=Path("src/frontend/ast.h"))
    parser.add_argument("--require-no-errors", action="store_true")
    return parser.parse_args()


def parse_enum_names(path: Path, enum_name: str) -> dict[int, str]:
    """Read a simple C enum so numeric trace kinds remain meaningful to humans."""
    try:
        text = path.read_text()
    except OSError:
        return {}

    match = re.search(
        rf"typedef\s+enum\s+{re.escape(enum_name)}\s*\{{(.*?)\}}\s*"
        rf"{re.escape(enum_name)}\s*;",
        text,
        re.DOTALL,
    )
    if match is None:
        return {}

    body = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
    names: dict[int, str] = {}
    value = -1
    for raw_item in body.split(","):
        item = re.sub(r"//.*", "", raw_item).strip()
        if not item:
            continue
        if "=" in item:
            name, raw_value = (part.strip() for part in item.split("=", 1))
            try:
                value = int(raw_value, 0)
            except ValueError:
                continue
        else:
            name = item
            value += 1
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            names[value] = name
    return names


def parse_trace_line(text: str) -> tuple[str, dict[str, str]] | None:
    marker_match = TRACE_MARKER_RE.match(text.strip())
    if marker_match is None:
        return None
    return marker_match.group(1), dict(TRACE_FIELD_RE.findall(text))


def symbolize_trace_fields(
    fields: dict[str, str],
    expression_names: dict[int, str],
    statement_names: dict[int, str],
) -> dict[str, str]:
    symbolic = dict(fields)
    for key in EXPRESSION_KIND_FIELDS:
        raw_value = symbolic.get(key)
        if raw_value is None:
            continue
        try:
            value = int(raw_value, 0)
        except ValueError:
            continue
        if value < 0:
            symbolic[key] = "none"
        elif value in expression_names:
            symbolic[key] = expression_names[value]

    if symbolic.get("stage") == "statement" and "kind" in symbolic:
        try:
            statement_kind = int(symbolic["kind"], 0)
        except ValueError:
            pass
        else:
            if statement_kind in statement_names:
                symbolic["kind"] = statement_names[statement_kind]
    return symbolic


def normalized_marker(marker: str, fields: dict[str, str]) -> str:
    parts = [
        f"{key}={value}"
        for key, value in fields.items()
        if key not in VOLATILE_TRACE_FIELDS
    ]
    return marker + ((" " + " ".join(parts)) if parts else "")


def trace_info(
    row: dict[str, object],
    stderr_root: Path | None,
    expression_names: dict[int, str],
    statement_names: dict[int, str],
) -> dict[str, object]:
    if stderr_root is None:
        return {
            "feature_family": "trace-unavailable",
            "reason_fingerprint": "trace-unavailable",
            "trace_lines": [],
        }

    stderr_path = stderr_root / f"{row.get('input', '')}.stderr"
    if not stderr_path.is_file():
        return {
            "feature_family": "trace-missing",
            "reason_fingerprint": "trace-missing",
            "trace_lines": [],
        }

    unsupported_match = UNSUPPORTED_RE.match(str(row.get("message", "")))
    target_function = unsupported_match.group(1) if unsupported_match else ""
    parsed: list[tuple[str, dict[str, str], str]] = []
    for raw_line in stderr_path.read_text(errors="replace").splitlines():
        trace = parse_trace_line(raw_line)
        if trace is None:
            continue
        marker, fields = trace
        if target_function and fields.get("function") != target_function:
            continue
        if fields.get("status") not in (None, "1"):
            continue
        parsed.append(
            (
                marker,
                symbolize_trace_fields(fields, expression_names, statement_names),
                raw_line.strip(),
            )
        )

    fast_trace = next((entry for entry in parsed if entry[0] == "CORE_FAST_TRACE"), None)
    specialized_trace = next((entry for entry in parsed if entry[0] != "CORE_FAST_TRACE"), None)

    if fast_trace is not None:
        _, fields, _ = fast_trace
        if fields.get("stage") == "statement" and "kind" in fields:
            family = f"statement:{fields['kind']}"
        elif "condition_kind" in fields:
            family = f"expression:{fields['condition_kind']}"
        else:
            family = normalized_marker("CORE_FAST_TRACE", fields)
    elif specialized_trace is not None:
        marker, fields, _ = specialized_trace
        expression_kind = next(
            (
                fields[key]
                for key in EXPRESSION_KIND_FIELDS
                if key in fields and fields[key] != "none"
            ),
            None,
        )
        family = (
            f"expression:{expression_kind}"
            if expression_kind is not None
            else normalized_marker(marker, fields)
        )
    else:
        family = "NO_CORE_TRACE"

    fingerprint_parts: list[str] = []
    if fast_trace is not None:
        fingerprint_parts.append(normalized_marker(fast_trace[0], fast_trace[1]))
    if specialized_trace is not None:
        fingerprint_parts.append(normalized_marker(specialized_trace[0], specialized_trace[1]))

    return {
        "feature_family": family,
        "reason_fingerprint": " | ".join(fingerprint_parts) if fingerprint_parts else family,
        "trace_lines": [entry[2] for entry in parsed[:8]],
    }


def main() -> int:
    args = parse_args()
    rows = json.loads(args.results.read_text())
    expression_names = parse_enum_names(args.ast_header, "MinicExpressionKind")
    statement_names = parse_enum_names(args.ast_header, "MinicStatementKind")

    unsupported: dict[str, list[dict[str, object]]] = defaultdict(list)
    feature_families: dict[str, list[dict[str, object]]] = defaultdict(list)
    reason_fingerprints: dict[str, list[dict[str, object]]] = defaultdict(list)
    errors: list[dict[str, object]] = []
    missing: list[dict[str, object]] = []
    enriched_unsupported: list[dict[str, object]] = []
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
        unsupported_match = UNSUPPORTED_RE.match(message)
        if unsupported_match is None:
            errors.append(row)
            continue

        enriched = dict(row)
        enriched["function"] = unsupported_match.group(1)
        enriched.update(trace_info(row, args.stderr_root, expression_names, statement_names))
        unsupported[str(enriched["function"])].append(enriched)
        feature_families[str(enriched["feature_family"])].append(enriched)
        reason_fingerprints[str(enriched["reason_fingerprint"])].append(enriched)
        enriched_unsupported.append(enriched)

    ranked_blockers = sorted(unsupported.items(), key=lambda item: (-len(item[1]), item[0]))
    ranked_features = sorted(
        feature_families.items(), key=lambda item: (-len(item[1]), item[0])
    )
    ranked_fingerprints = sorted(
        reason_fingerprints.items(), key=lambda item: (-len(item[1]), item[0])
    )

    print(
        "CORE_STRICT500_SUMMARY "
        f"selected={len(rows)} pass={passes} "
        f"unsupported={len(enriched_unsupported)} "
        f"error={len(errors)} preprocess_missing={len(missing)} "
        f"distinct_blockers={len(ranked_blockers)} "
        f"distinct_feature_families={len(ranked_features)}"
    )
    for rank, (function, group) in enumerate(ranked_blockers, 1):
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
    for rank, (family, group) in enumerate(ranked_features, 1):
        functions = sorted({str(row["function"]) for row in group})
        print(
            "CORE_STRICT500_FEATURE "
            f"rank={rank} count={len(group)} family={family} functions={len(functions)}"
        )
        for row in group[: max(0, args.examples)]:
            print(
                "CORE_STRICT500_FEATURE_EXAMPLE "
                f"family={family} index={row['index']} input={row['input']} "
                f"function={row['function']}"
            )

    summary = {
        "selected": len(rows),
        "pass": passes,
        "unsupported": len(enriched_unsupported),
        "error": len(errors),
        "preprocess_missing": len(missing),
        "blockers": [
            {
                "function": function,
                "count": len(group),
                "indices": [row["index"] for row in group],
                "inputs": [row["input"] for row in group],
            }
            for function, group in ranked_blockers
        ],
        "feature_families": [
            {
                "family": family,
                "count": len(group),
                "functions": sorted({str(row["function"]) for row in group}),
                "indices": [row["index"] for row in group],
                "inputs": [row["input"] for row in group],
            }
            for family, group in ranked_features
        ],
        "reason_fingerprints": [
            {
                "fingerprint": fingerprint,
                "count": len(group),
                "functions": sorted({str(row["function"]) for row in group}),
                "indices": [row["index"] for row in group],
                "inputs": [row["input"] for row in group],
            }
            for fingerprint, group in ranked_fingerprints
        ],
        "unsupported_rows": enriched_unsupported,
        "errors": errors,
        "missing": missing,
    }
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")

    if args.reason_text_out is not None:
        lines = [
            "CORE_STRICT500_UNSUPPORTED_REASON_CENSUS",
            f"unsupported={len(enriched_unsupported)}",
            f"distinct_feature_families={len(ranked_features)}",
            f"distinct_reason_fingerprints={len(ranked_fingerprints)}",
            "",
            "feature_families:",
        ]
        for rank, (family, group) in enumerate(ranked_features, 1):
            functions = sorted({str(row["function"]) for row in group})
            lines.append(f"  {rank:>3}. count={len(group):>3} family={family}")
            lines.append("       functions=" + ", ".join(functions[:12]))
        lines.extend(["", "reason_fingerprints:"])
        for rank, (fingerprint, group) in enumerate(ranked_fingerprints, 1):
            lines.append(f"  {rank:>3}. count={len(group):>3} {fingerprint}")
        args.reason_text_out.parent.mkdir(parents=True, exist_ok=True)
        args.reason_text_out.write_text("\n".join(lines) + "\n")

    if args.require_no_errors and (errors or missing):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
