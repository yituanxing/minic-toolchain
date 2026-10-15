#!/usr/bin/env python3
"""Replay MiniC against a frozen set of preprocessed Linux translation units."""

from __future__ import annotations

from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import time


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--minic", required=True, type=Path)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--jobs", required=True, type=int)
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--sample-count", type=int, default=0)
    parser.add_argument("--indices", default="")
    return parser.parse_args()


def parse_indices(raw: str) -> list[int]:
    if not raw.strip():
        return []
    values: list[int] = []
    seen: set[int] = set()
    for token in raw.split(","):
        token = token.strip()
        if not token:
            raise ValueError("indices contains an empty entry")
        value = int(token)
        if value < 0:
            raise ValueError("indices must be non-negative")
        if value in seen:
            raise ValueError(f"duplicate configured TU index: {value}")
        seen.add(value)
        values.append(value)
    return values


def select_entries(
    entries: list[tuple[int, str, str, str]],
    *,
    offset: int,
    limit: int,
    sample_count: int,
    indices: str,
) -> list[tuple[int, str, str, str]]:
    if offset < 0 or limit < 0 or sample_count < 0:
        raise ValueError("offset, limit, and sample-count must be non-negative")

    requested = parse_indices(indices)
    if requested:
        by_index = {entry[0]: entry for entry in entries}
        missing = [index for index in requested if index not in by_index]
        if missing:
            raise ValueError(
                "requested configured TU indices are absent from frozen corpus: "
                + ",".join(str(index) for index in missing)
            )
        selected = [by_index[index] for index in requested]
    else:
        if offset >= len(entries):
            selected = []
        else:
            stop = len(entries) if limit == 0 else min(len(entries), offset + limit)
            selected = entries[offset:stop]

    if sample_count == 0 or sample_count >= len(selected):
        return selected
    if sample_count == 1:
        return selected[:1]

    last = len(selected) - 1
    positions = [sample * last // (sample_count - 1) for sample in range(sample_count)]
    return [selected[position] for position in positions]


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def count_physical_lines(path: Path) -> int:
    """Count physical lines in a frozen .i without loading the whole TU."""
    count = 0
    last_byte = b""
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            count += chunk.count(b"\n")
            if chunk:
                last_byte = chunk[-1:]
    if path.stat().st_size > 0 and last_byte != b"\n":
        count += 1
    return count


def progress_ratio(
    first_error_line: int | None,
    total_lines: int | None,
    *,
    passed: bool,
) -> float | None:
    if passed:
        return 1.0
    if first_error_line is None or total_lines is None or total_lines <= 0:
        return None
    return min(1.0, max(0.0, first_error_line / total_lines))


def emit_progress(done: int, total: int) -> None:
    width = 20
    filled = width if total == 0 else int(done * width / total)
    pct = 100 if total == 0 else int(done * 100 / total)
    bar = "#" * filled + "." * (width - filled)
    print(
        f"LINUX_PROGRESS phase=minic-replay [{bar}] {pct}% "
        f"done={done} total={total}",
        flush=True,
    )

def main() -> int:
    args = parse_args()
    started = time.monotonic()
    if args.jobs <= 0:
        raise SystemExit("jobs must be greater than zero")
    if not args.minic.is_file():
        raise SystemExit(f"missing MiniC binary: {args.minic}")
    minic_sha256 = file_sha256(args.minic)

    manifest = args.corpus / "selected-tus.txt"
    input_root = args.corpus / "kbuild"
    if not manifest.is_file():
        raise SystemExit(f"missing frozen corpus manifest: {manifest}")

    entries: list[tuple[int, str, str, str]] = []
    for line in manifest.read_text().splitlines():
        index, obj, preprocessed, source = line.split("\t", 3)
        entries.append((int(index), obj, preprocessed, source))
    if not entries:
        raise SystemExit("frozen corpus manifest is empty")
    try:
        entries = select_entries(
            entries,
            offset=args.offset,
            limit=args.limit,
            sample_count=args.sample_count,
            indices=args.indices,
        )
    except (TypeError, ValueError) as error:
        raise SystemExit(f"invalid frozen corpus selection: {error}") from error
    if not entries:
        raise SystemExit("frozen corpus selection is empty")
    selected_manifest = "".join(
        f"{index}\t{obj}\t{preprocessed}\t{source}\n"
        for index, obj, preprocessed, source in entries
    )

    args.work.mkdir(parents=True, exist_ok=True)
    asm_root = args.work / "minic-out"
    log_root = args.work / "minic-stderr"
    asm_root.mkdir(parents=True, exist_ok=True)
    log_root.mkdir(parents=True, exist_ok=True)
    diag_re = re.compile(r"^(.*?):([0-9]+):([0-9]+):\s*(?:error:\s*)?(.*)$")
    core_trace_re = re.compile(r"\bCORE_FAST_TRACE\b.*?\bspan=([0-9]+):([0-9]+)\b")

    def compile_one(entry: tuple[int, str, str, str]) -> dict[str, object]:
        index, obj, rel_i, source = entry
        input_path = input_root / rel_i
        asm_path = asm_root / f"{rel_i}.s"
        stderr_path = log_root / f"{rel_i}.stderr"
        asm_path.parent.mkdir(parents=True, exist_ok=True)
        stderr_path.parent.mkdir(parents=True, exist_ok=True)
        if not input_path.is_file() or input_path.stat().st_size == 0:
            return {
                "index": index,
                "object": obj,
                "input": rel_i,
                "source": source,
                "status": "PREPROCESS_MISSING",
                "returncode": None,
                "total_lines": None,
                "first_error_line": None,
                "first_error_column": None,
                "first_error_source": None,
                "progress_ratio": None,
                "line": None,
                "column": None,
                "message": "frozen Linux corpus does not contain selected .i",
                "seconds": 0.0,
            }

        total_lines = count_physical_lines(input_path)
        compile_started = time.monotonic()
        proc = subprocess.run(
            [str(args.minic), "-S", str(input_path), "-o", str(asm_path)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
            check=False,
        )
        elapsed = time.monotonic() - compile_started
        stderr_path.write_text(proc.stderr)
        if proc.returncode == 0 and asm_path.is_file() and asm_path.stat().st_size > 0:
            return {
                "index": index,
                "object": obj,
                "input": rel_i,
                "source": source,
                "status": "PASS",
                "returncode": 0,
                "total_lines": total_lines,
                "first_error_line": None,
                "first_error_column": None,
                "first_error_source": None,
                "progress_ratio": 1.0,
                "line": None,
                "column": None,
                "message": "",
                "seconds": elapsed,
            }

        diagnostic_line = None
        diagnostic_column = None
        diagnostic_message = None
        first_nontrace = None
        trace_line = None
        trace_column = None
        for raw_line in proc.stderr.splitlines():
            text = raw_line.strip()
            if not text:
                continue
            if trace_line is None:
                trace_match = core_trace_re.search(text)
                if trace_match:
                    trace_line = int(trace_match.group(1))
                    trace_column = int(trace_match.group(2))
            match = diag_re.match(text)
            if match and diagnostic_message is None:
                diagnostic_line = int(match.group(2))
                diagnostic_column = int(match.group(3))
                diagnostic_message = match.group(4).strip() or text
                continue
            if "CORE_FAST_TRACE" not in text and first_nontrace is None:
                first_nontrace = text

        message = (
            diagnostic_message
            or first_nontrace
            or f"MiniC failed without diagnostic (returncode={proc.returncode})"
        )
        if trace_line is not None:
            first_error_line = trace_line
            first_error_column = trace_column
            first_error_source = "core_trace"
        else:
            first_error_line = diagnostic_line
            first_error_column = diagnostic_column
            first_error_source = "diagnostic" if diagnostic_line is not None else None
        ratio = progress_ratio(first_error_line, total_lines, passed=False)
        return {
            "index": index,
            "object": obj,
            "input": rel_i,
            "source": source,
            "status": "FAIL",
            "returncode": proc.returncode,
            "total_lines": total_lines,
            "first_error_line": first_error_line,
            "first_error_column": first_error_column,
            "first_error_source": first_error_source,
            "progress_ratio": ratio,
            # Compatibility aliases for existing classifiers/readers.
            "line": first_error_line,
            "column": first_error_column,
            "message": message,
            "seconds": elapsed,
        }

    results: list[dict[str, object]] = []
    total = len(entries)
    report_every = 1 if total <= 32 else max(1, total // 20)
    completed = 0
    emit_progress(0, total)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_one, entry) for entry in entries]
        for future in as_completed(futures):
            results.append(future.result())
            completed += 1
            if completed == total or completed % report_every == 0:
                emit_progress(completed, total)
    results.sort(key=lambda row: int(row["index"]))

    counts = Counter(str(row["status"]) for row in results)
    groups = Counter(str(row["message"]) for row in results if row["status"] == "FAIL")

    with (args.work / "batch-results.tsv").open("w") as output:
        output.write(
            "index\tstatus\tinput\tsource\ttotal_lines\tfirst_error_line\tfirst_error_column\t"
            "first_error_source\tprogress_ratio\tline\tcolumn\treturncode\tseconds\tmessage\n"
        )
        for row in results:
            message = str(row["message"]).replace("\t", " ").replace("\n", " ")
            total_lines = "" if row["total_lines"] is None else str(row["total_lines"])
            first_error_line = (
                "" if row["first_error_line"] is None else str(row["first_error_line"])
            )
            first_error_column = (
                "" if row["first_error_column"] is None else str(row["first_error_column"])
            )
            first_error_source = (
                "" if row["first_error_source"] is None else str(row["first_error_source"])
            )
            ratio = (
                ""
                if row["progress_ratio"] is None
                else f'{float(row["progress_ratio"]):.6f}'
            )
            line = "" if row["line"] is None else str(row["line"])
            column = "" if row["column"] is None else str(row["column"])
            returncode = "" if row["returncode"] is None else str(row["returncode"])
            output.write(
                f'{row["index"]}\t{row["status"]}\t{row["input"]}\t{row["source"]}\t'
                f'{total_lines}\t{first_error_line}\t{first_error_column}\t{first_error_source}\t'
                f'{ratio}\t{line}\t{column}\t{returncode}\t{float(row["seconds"]):.3f}\t{message}\n'
            )
    (args.work / "batch-results.json").write_text(json.dumps(results, indent=2, sort_keys=True))
    (args.work / "selected-tus.txt").write_text(selected_manifest)
    corpus_manifest = args.corpus / "tu-manifest.txt"
    if corpus_manifest.is_file():
        shutil.copy2(corpus_manifest, args.work / "tu-manifest.txt")

    context_lines: list[str] = []
    for row in results:
        if row["status"] != "FAIL" or row["first_error_line"] is None:
            continue
        input_path = input_root / str(row["input"])
        try:
            lines = input_path.read_text(errors="replace").splitlines()
        except OSError:
            continue
        line_index = int(row["first_error_line"]) - 1
        start = max(0, line_index - 3)
        end = min(len(lines), line_index + 4)
        context_lines.append(
            f'=== index={row["index"]} input={row["input"]} '
            f'first_error_line={row["first_error_line"]} total_lines={row["total_lines"]} '
            f'locator={row["first_error_source"]} message={row["message"]} ==='
        )
        for current in range(start, end):
            marker = ">" if current == line_index else " "
            context_lines.append(f"{marker}{current + 1:8d}: {lines[current]}")
        context_lines.append("")
    (args.work / "failure-contexts.txt").write_text("\n".join(context_lines))

    corpus_bytes = sum(path.stat().st_size for path in input_root.rglob("*.i"))
    selected_corpus_bytes = sum((input_root / entry[2]).stat().st_size for entry in entries)
    replay_seconds = time.monotonic() - started
    summary_lines = [
        "LINUX_BATCH_SUMMARY",
        "corpus=frozen",
        f"minic_sha256={minic_sha256}",
        f"corpus_bytes={corpus_bytes}",
        f"selected_corpus_bytes={selected_corpus_bytes}",
        f"replay_seconds={replay_seconds:.3f}",
        f"selected_c_tus={len(results)}",
        f"pass={counts['PASS']}",
        f"fail={counts['FAIL']}",
        f"preprocess_missing={counts['PREPROCESS_MISSING']}",
        f"minic_jobs={args.jobs}",
        "",
        "minic_failure_groups:",
    ]
    if groups:
        for message, count in groups.most_common():
            summary_lines.append(f"  count={count} message={message}")
    else:
        summary_lines.append("  none")
    summary_lines.extend(["", "failures:"])
    for row in results:
        if row["status"] == "PASS":
            continue
        first_error_line = (
            "unknown" if row["first_error_line"] is None else str(row["first_error_line"])
        )
        total_lines = "unknown" if row["total_lines"] is None else str(row["total_lines"])
        locator = (
            "unknown" if row["first_error_source"] is None else str(row["first_error_source"])
        )
        ratio = (
            "unknown"
            if row["progress_ratio"] is None
            else f'{float(row["progress_ratio"]) * 100.0:.2f}%'
        )
        returncode = "none" if row["returncode"] is None else str(row["returncode"])
        summary_lines.append(
            f'  index={row["index"]} status={row["status"]} input={row["input"]} '
            f'first_error_line={first_error_line} total_lines={total_lines} progress={ratio} '
            f'locator={locator} rc={returncode} message={row["message"]}'
        )
    if counts["FAIL"] == 0 and counts["PREPROCESS_MISSING"] == 0:
        summary_lines.append("  none")

    summary_lines.extend(["", "tu_progress:"])
    for row in results:
        first_error_line = "-" if row["first_error_line"] is None else str(row["first_error_line"])
        total_lines = "-" if row["total_lines"] is None else str(row["total_lines"])
        locator = "-" if row["first_error_source"] is None else str(row["first_error_source"])
        ratio = (
            "-"
            if row["progress_ratio"] is None
            else f'{float(row["progress_ratio"]) * 100.0:.2f}%'
        )
        summary_lines.append(
            f'  index={row["index"]} status={row["status"]} input={row["input"]} '
            f'total_lines={total_lines} first_error_line={first_error_line} '
            f'progress={ratio} locator={locator}'
        )

    summary = "\n".join(summary_lines) + "\n"
    (args.work / "batch-summary.txt").write_text(summary)
    print(
        f"LINUX_BATCH_CORPUS_REPLAY selected={len(results)} "
        f"selected_bytes={selected_corpus_bytes} corpus_bytes={corpus_bytes} "
        f"seconds={replay_seconds:.3f} minic_sha256={minic_sha256}"
    )
    print(summary, end="")
    return 1 if counts["FAIL"] or counts["PREPROCESS_MISSING"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
