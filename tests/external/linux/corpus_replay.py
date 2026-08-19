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
    return parser.parse_args()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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

    args.work.mkdir(parents=True, exist_ok=True)
    asm_root = args.work / "minic-out"
    log_root = args.work / "minic-stderr"
    asm_root.mkdir(parents=True, exist_ok=True)
    log_root.mkdir(parents=True, exist_ok=True)
    diag_re = re.compile(r"^(.*?):([0-9]+):([0-9]+):\s*(?:error:\s*)?(.*)$")

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
                "line": None,
                "column": None,
                "message": "frozen Linux corpus does not contain selected .i",
                "seconds": 0.0,
            }

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
                "line": None,
                "column": None,
                "message": "",
                "seconds": elapsed,
            }

        first = f"MiniC failed without diagnostic (returncode={proc.returncode})"
        line_number = None
        column_number = None
        for raw_line in proc.stderr.splitlines():
            text = raw_line.strip()
            if not text:
                continue
            match = diag_re.match(text)
            if match:
                line_number = int(match.group(2))
                column_number = int(match.group(3))
                first = match.group(4).strip() or text
                break
            if first.startswith("MiniC failed without diagnostic"):
                first = text
        return {
            "index": index,
            "object": obj,
            "input": rel_i,
            "source": source,
            "status": "FAIL",
            "returncode": proc.returncode,
            "line": line_number,
            "column": column_number,
            "message": first,
            "seconds": elapsed,
        }

    results: list[dict[str, object]] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_one, entry) for entry in entries]
        for future in as_completed(futures):
            results.append(future.result())
    results.sort(key=lambda row: int(row["index"]))

    counts = Counter(str(row["status"]) for row in results)
    groups = Counter(str(row["message"]) for row in results if row["status"] == "FAIL")

    with (args.work / "batch-results.tsv").open("w") as output:
        output.write("index\tstatus\tinput\tsource\tline\tcolumn\treturncode\tseconds\tmessage\n")
        for row in results:
            message = str(row["message"]).replace("\t", " ").replace("\n", " ")
            line = "" if row["line"] is None else str(row["line"])
            column = "" if row["column"] is None else str(row["column"])
            returncode = "" if row["returncode"] is None else str(row["returncode"])
            output.write(
                f'{row["index"]}\t{row["status"]}\t{row["input"]}\t{row["source"]}\t'
                f'{line}\t{column}\t{returncode}\t{float(row["seconds"]):.3f}\t{message}\n'
            )
    (args.work / "batch-results.json").write_text(json.dumps(results, indent=2, sort_keys=True))
    (args.work / "selected-tus.txt").write_text(manifest.read_text())
    corpus_manifest = args.corpus / "tu-manifest.txt"
    if corpus_manifest.is_file():
        shutil.copy2(corpus_manifest, args.work / "tu-manifest.txt")

    context_lines: list[str] = []
    for row in results:
        if row["status"] != "FAIL" or row["line"] is None:
            continue
        input_path = input_root / str(row["input"])
        try:
            lines = input_path.read_text(errors="replace").splitlines()
        except OSError:
            continue
        line_index = int(row["line"]) - 1
        start = max(0, line_index - 3)
        end = min(len(lines), line_index + 4)
        context_lines.append(
            f'=== index={row["index"]} input={row["input"]} '
            f'line={row["line"]} message={row["message"]} ==='
        )
        for current in range(start, end):
            marker = ">" if current == line_index else " "
            context_lines.append(f"{marker}{current + 1:8d}: {lines[current]}")
        context_lines.append("")
    (args.work / "failure-contexts.txt").write_text("\n".join(context_lines))

    corpus_bytes = sum(path.stat().st_size for path in input_root.rglob("*.i"))
    replay_seconds = time.monotonic() - started
    summary_lines = [
        "LINUX_BATCH_SUMMARY",
        "corpus=frozen",
        f"minic_sha256={minic_sha256}",
        f"corpus_bytes={corpus_bytes}",
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
        line = "unknown" if row["line"] is None else str(row["line"])
        returncode = "none" if row["returncode"] is None else str(row["returncode"])
        summary_lines.append(
            f'  index={row["index"]} status={row["status"]} input={row["input"]} '
            f'line={line} rc={returncode} message={row["message"]}'
        )
    if counts["FAIL"] == 0 and counts["PREPROCESS_MISSING"] == 0:
        summary_lines.append("  none")

    summary = "\n".join(summary_lines) + "\n"
    (args.work / "batch-summary.txt").write_text(summary)
    print(
        f"LINUX_BATCH_CORPUS_REPLAY selected={len(results)} bytes={corpus_bytes} "
        f"seconds={replay_seconds:.3f} minic_sha256={minic_sha256}"
    )
    print(summary, end="")
    return 1 if counts["FAIL"] or counts["PREPROCESS_MISSING"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
