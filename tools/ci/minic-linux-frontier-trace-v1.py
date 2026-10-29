#!/usr/bin/env python3
"""Focused, SHA-locked diagnostics from an existing three-ISA GNU .i corpus.

This does not preprocess Linux, alter the frozen inputs, or assert that -S
success proves assembly/link/runtime correctness. Raw MiniC stderr and a small
source-context excerpt per selected C owner are archived as compressed JSONL.
"""

import argparse
import collections
import concurrent.futures
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile


ERROR_NESTED = "cannot record nested static symbolic relocation"
# GNU -E -P preserves arbitrary newlines between `asm`, modifiers, and `(`.
# Match the keyword itself, then keep following lines with templates/constraints.
ASM_SOURCE = re.compile(r"\b(?:__asm__|asm)\b")
LINE_NUMBER = re.compile(r"(?:^|\s|/)[^\s:]*\.(?:i|c|h):(\d+)(?::\d+)?:", re.M)
TRACE_PREFIXES = (
    "STATIC_RELOC_DETAIL ", "CORE_FAST_TRACE ", "CORE_ASM_FRONTIER ",
    "MINIC_CORE_FUNCTION_FAILURE ", "MINIC_BACKEND_PREFLIGHT_ROOT ",
    "MINIC_BACKEND_ASM_FORM ", "MINIC_BACKEND_REJECT ",
    "MINIC_M8_OPCODE_REJECT ",
)


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def classify(target, row):
    if row.get("status") != "MINIC_FAIL":
        return None
    diag = row.get("diagnostic", "").lower()
    core = row.get("core_frontiers") or []
    asm = row.get("asm_frontiers") or []
    preflight = row.get("preflight_roots") or []
    backend = row.get("backend_frontiers") or []
    machine = row.get("machine_frontiers") or []
    if target == "x86_64":
        if any(f.get("kind") == "UNSUPPORTED" for f in core):
            return "x86_core_unsupported_asm"
        if "bit-field" in diag or "bitfield" in diag or "bit field" in diag:
            return "x86_bitfield"
        if "integer initializer" in diag:
            return "x86_integer_initializer"
        if asm or preflight or "inline asm" in diag:
            return "x86_asm_other"
    elif target == "aarch64":
        if ERROR_NESTED in diag:
            return "aarch64_nested_static_relocation"
        if core or asm or preflight or backend or machine or any(
            word in diag for word in ("inline asm", "llsc", "ll/sc", "lse", "atomic")
        ):
            return "aarch64_asm_atomic"
    elif target == "loongarch64":
        if "jump_table" in diag or "jump table" in diag or "jump-table" in diag:
            return "loongarch64_jump_table"
        if core or asm or preflight or backend or machine:
            return "loongarch64_core_machine"
    return target + "_other_source"


def source_context(path, stderr, *, max_error_sites=3, max_asm_sites=5):
    """Keep diagnostic location and first asm templates with constraints.

    GNU -E -P strips #line markers, so line numbers are from the frozen .i.
    If the compiler points to end-of-file, scan early assembler forms as a
    fallback rather than inventing an AST-to-source location.
    """
    source_lines = sorted({int(m.group(1)) for m in LINE_NUMBER.finditer(stderr)})[:max_error_sites]
    contexts = []
    preceding = collections.deque(maxlen=4)
    asm_remaining = 0
    error_remaining = 0
    asm_count = 0
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line_number, line in enumerate(stream, 1):
            error_hit = any(abs(line_number - index) <= 5 for index in source_lines)
            asm_hit = asm_count < max_asm_sites and ASM_SOURCE.search(line) is not None
            if asm_hit:
                asm_count += 1
                asm_remaining = max(asm_remaining, 7)
                for n, prior in preceding:
                    contexts.append({"line": n, "kind": "asm_before", "text": prior[:1600]})
            if error_hit:
                error_remaining = max(error_remaining, 1)
            if error_hit or asm_hit or asm_remaining or error_remaining:
                contexts.append({"line": line_number,
                                 "kind": "error" if error_hit else "asm",
                                 "text": line.rstrip("\n")[:2400]})
            if asm_remaining:
                asm_remaining -= 1
            if error_remaining:
                error_remaining -= 1
            preceding.append((line_number, line.rstrip("\n")))
            if len(contexts) >= 120:
                break
    # Duplicates occur when several diagnostics point into the same asm form.
    dedup = {(v["line"], v["kind"]): v for v in contexts}
    return list(dedup.values())


def parse_report_directory(path, target, frozen, minic_path, expected_head):
    manifests = json.loads((frozen / "manifest.json").read_text())
    if manifests.get("schema") != "three-isa-gnu-i-v1" or manifests.get("errors"):
        raise ValueError("incomplete frozen manifest")
    if manifests["identity"]["target"] != target:
        raise ValueError("wrong frozen target")
    digest = sha256_file(frozen / "manifest.json")
    compiler_sha = sha256_file(minic_path)
    files = sorted(path.glob("**/report.json"))
    if len(files) != 7:
        raise ValueError(f"expected seven reports, found {len(files)}")
    reports = [json.loads(file.read_text()) for file in files]
    if {report.get("shard") for report in reports} != set(range(7)):
        raise ValueError("missing or repeated shard index")
    for report in reports:
        if (report.get("target") != target or report.get("frozen_identity") != manifests["identity"]
                or report.get("frozen_manifest_sha256") != digest
                or report.get("compiler_sha256") != compiler_sha
                or report.get("frozen_total") != len(manifests["rows"])):
            raise ValueError("mismatched frozen/compiler identity for shard")
    report_rows = {row["index"]: row for report in reports for row in report["rows"]}
    frozen_rows = {row["index"]: row for row in manifests["rows"]}
    if report_rows.keys() != frozen_rows.keys():
        raise ValueError("seven reports do not cover exact frozen ownership")
    for index, row in report_rows.items():
        saved = frozen_rows[index]
        if (row["object"] != saved["object"] or row["input_sha256"] != saved["sha256"]
                or saved["shard"] != index % 7):
            raise ValueError("source SHA or C owner changed: " + row["object"])
    selected = []
    for index, row in report_rows.items():
        kind = classify(target, row)
        if kind is not None:
            selected.append((kind, frozen_rows[index], row))
    eligible_total = len(selected)
    # Current broad census has at most 559 failed owners per ISA. A strict
    # generous budget guards accidental full-replay while retaining every
    # actual C owner, including unexpected diagnostics after the first fix.
    if len(selected) > 1200:
        raise ValueError(f"focused frontier too broad: {len(selected)} > 1200")
    selected.sort(key=lambda item: item[1]["index"])
    return manifests, compiler_sha, digest, selected, eligible_total


def run_one(item, target, frozen, minic_path, timeout):
    kind, saved, prior = item
    path = PurePosixPath(saved["input"])
    if path.is_absolute() or ".." in path.parts or not path.parts or path.parts[0] != "i":
        raise ValueError("unsafe frozen source path")
    source = frozen / path
    if source.is_symlink() or sha256_file(source) != saved["sha256"]:
        raise ValueError("frozen source SHA mismatch: " + saved["object"])
    allowed_flags = {
        "-fshort-wchar", "-fno-short-wchar", "-fsigned-char", "-funsigned-char",
        "-fno-signed-char", "-fno-unsigned-char", "-ffunction-sections",
        "-fno-function-sections", "-fPIE", "-fpie", "-fPIC", "-fpic",
        "-fno-pie", "-fno-PIE", "-fno-pic", "-fno-PIC",
    }
    flags = saved["minic_flags"]
    if any(flag not in allowed_flags for flag in flags):
        raise ValueError("unsupported frozen ABI option: " + saved["object"])
    env = os.environ.copy()
    env.update(CORE_FAST_TRACE="1", MINIC_CORE_FAILURE_CENSUS="1",
               MINIC_ASM_FRONTIER_TRACE="1", MINIC_BACKEND_CENSUS="1",
               MINIC_M1_TRACE="1")
    with tempfile.TemporaryDirectory(prefix="minic-frontier-") as tmp:
        output = Path(tmp) / "focused.s"
        argv = [str(minic_path), "--target=" + target + "-linux", *flags,
                "-S", str(source), "-o", str(output)]
        try:
            result = subprocess.run(argv, capture_output=True, timeout=timeout, env=env)
            stdout = result.stdout.decode("utf-8", errors="replace")
            stderr = result.stderr.decode("utf-8", errors="replace")
            code = result.returncode
        except subprocess.TimeoutExpired as exc:
            stdout = (exc.stdout or b"").decode("utf-8", errors="replace")
            stderr = (exc.stderr or b"").decode("utf-8", errors="replace")
            code = "timeout"
    lines = stderr.splitlines()
    trace = [line for line in lines if line.startswith(TRACE_PREFIXES)]
    diagnostics = [line for line in lines if "error:" in line.lower() or
                   "warning:" in line.lower()]
    return {
        "index": saved["index"], "object": saved["object"], "input": str(path),
        "sha256": saved["sha256"], "class": kind, "minic_flags": flags,
        "previous_diagnostic": prior["diagnostic"], "exit_code": code,
        "stderr_sha256": hashlib.sha256(stderr.encode()).hexdigest(),
        "stdout": stdout, "stderr": stderr, "diagnostics": diagnostics,
        "trace": trace, "source_context": source_context(source, stderr),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=("x86_64", "aarch64", "loongarch64"), required=True)
    parser.add_argument("--frozen", type=Path, required=True)
    parser.add_argument("--reports", type=Path, required=True)
    parser.add_argument("--minic", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-head", required=True)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    try:
        manifests, compiler_sha, manifest_sha, selected, eligible_total = parse_report_directory(
            args.reports, args.target, args.frozen, args.minic, args.expected_head)
        args.output.mkdir(parents=True, exist_ok=True)
        compressed = args.output / "frontier-stderr-context.jsonl.gz"
        actual = collections.Counter()
        with gzip.open(compressed, "wt", encoding="utf-8", compresslevel=6) as output:
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as executor:
                for row in executor.map(lambda item: run_one(
                        item, args.target, args.frozen, args.minic, args.timeout), selected):
                    actual[row["class"]] += 1
                    output.write(json.dumps(row, ensure_ascii=False) + "\n")
        summary = {
            "schema": "minic-three-isa-targeted-frontier-v1",
            "target": args.target, "artifact_head": args.expected_head,
            "frozen_identity": manifests["identity"],
            "frozen_manifest_sha256": manifest_sha,
            "compiler_sha256": compiler_sha,
            "frozen_total": len(manifests["rows"]),
            "eligible": eligible_total, "selected": len(selected),
            "omitted_by_budget": eligible_total - len(selected),
            "captured_classes": dict(sorted(actual.items())),
            "archive": compressed.name,
            "archive_bytes": compressed.stat().st_size,
        }
        (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        print("MINIC_FOCUSED_FRONTIER " + json.dumps(summary))
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        print("MINIC_FOCUSED_FRONTIER=FAIL " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
