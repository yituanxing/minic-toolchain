#!/usr/bin/env python3
"""Static census for the MiniC-generated RISC-V assembly surface.

This is development/validation infrastructure, not part of the production
assembler.  It intentionally describes the assembly language actually emitted
by the frozen compiler before MiniAS implements any of it.
"""

from __future__ import annotations

import argparse
import json
import re
import time
from collections import Counter
from pathlib import Path
from typing import Iterable

SCHEMA_VERSION = 1
EXAMPLE_LIMIT = 4

KNOWN_PSEUDOS = {
    "beqz", "bgez", "bgt", "bgtu", "bgtz", "ble", "bleu", "blez", "bltz",
    "bnez", "call", "j", "jr", "la", "lla", "li", "mv", "neg", "negw",
    "nop", "not", "ret", "seqz", "sext.b", "sext.h", "sext.w", "sgtz",
    "sltz", "snez", "tail", "zext.b", "zext.h", "zext.w",
}

DIRECT_SECTION_DIRECTIVES = {".text", ".data", ".bss", ".rodata", ".sdata", ".sbss"}
SECTION_DIRECTIVES = {".section", ".pushsection"}

LABEL_RE = re.compile(r"^\s*(?:[.$A-Za-z_][.$A-Za-z0-9_]*|[0-9]+):\s*")
DIRECTIVE_RE = re.compile(r"^\s*(\.[A-Za-z0-9_][A-Za-z0-9_.]*)\b(.*)$")
MNEMONIC_RE = re.compile(r"^\s*([A-Za-z][A-Za-z0-9_.]*)\b(.*)$")
RELOC_RE = re.compile(r"%([A-Za-z_][A-Za-z0-9_]*)\s*\(")
AT_SUFFIX_RE = re.compile(r"@([A-Za-z_][A-Za-z0-9_]*)")
NUMERIC_LOCAL_RE = re.compile(r"(?<![A-Za-z0-9_])\d+[fb](?![A-Za-z0-9_])")
SYMBOL = r"(?:[.$A-Za-z_][.$A-Za-z0-9_]*)"
SYMBOL_DIFF_RE = re.compile(SYMBOL + r"\s*-\s*" + SYMBOL)
SYMBOL_ADDEND_RE = re.compile(SYMBOL + r"\s*[+-]\s*(?:0[xX][0-9A-Fa-f]+|\d+)")
DOT_EXPR_RE = re.compile(r"(?:^|[,\s])\.\s*[+-]")


def strip_comment(line: str) -> str:
    """Remove a GAS '#' comment without touching quoted strings."""
    if "#" not in line:
        return line
    quote = None
    escaped = False
    out: list[str] = []
    for ch in line:
        if escaped:
            out.append(ch)
            escaped = False
            continue
        if ch == "\\":
            out.append(ch)
            escaped = True
            continue
        if quote is not None:
            out.append(ch)
            if ch == quote:
                quote = None
            continue
        if ch in {'"', "'"}:
            out.append(ch)
            quote = ch
            continue
        if ch == "#":
            break
        out.append(ch)
    return "".join(out)


def first_operand(text: str) -> str:
    text = text.strip()
    if not text:
        return ""
    quote = None
    escaped = False
    for index, ch in enumerate(text):
        if escaped:
            escaped = False
            continue
        if ch == "\\":
            escaped = True
            continue
        if quote is not None:
            if ch == quote:
                quote = None
            continue
        if ch in {'"', "'"}:
            quote = ch
            continue
        if ch == ",":
            return text[:index].strip()
    return text.strip()


def normalize_section_name(raw: str) -> str:
    raw = raw.strip()
    if len(raw) >= 2 and raw[0] == raw[-1] == '"':
        raw = raw[1:-1]
    return raw or "<empty>"

def section_family(section: str) -> str:
    """Collapse symbol-specific section names for syntax coverage only."""
    for prefix in (
        ".text", ".rodata", ".data", ".bss", ".sdata", ".sbss",
        ".init", ".exit", ".discard", ".debug", ".note",
    ):
        if section == prefix:
            return prefix
        if section.startswith(prefix + "."):
            return prefix + ".*"
    return "<custom-section>"


def add_example(examples: dict[str, list[dict[str, object]]], feature: str,
                file_key: str, line_no: int, text: str) -> None:
    bucket = examples.setdefault(feature, [])
    if len(bucket) >= EXAMPLE_LIMIT:
        return
    bucket.append({"file": file_key, "line": line_no, "text": text.rstrip()})


def expression_features(text: str) -> set[str]:
    """Classify expression shapes with cheap character guards before regex."""
    features: set[str] = set()
    if "%" in text and RELOC_RE.search(text):
        features.add("expr:reloc-function")
    if ("f" in text or "b" in text) and NUMERIC_LOCAL_RE.search(text):
        features.add("expr:numeric-local-ref")
    if "-" in text and SYMBOL_DIFF_RE.search(text):
        features.add("expr:symbol-difference")
    if ("+" in text or "-" in text) and SYMBOL_ADDEND_RE.search(text):
        features.add("expr:symbol-addend")
    if "." in text and ("+" in text or "-" in text) and DOT_EXPR_RE.search(text):
        features.add("expr:dot-relative")
    if "(" in text and ")" in text:
        features.add("expr:parenthesized")
    return features


def empty_totals() -> dict[str, object]:
    return {
        "files": 0,
        "physical_lines": 0,
        "nonempty_lines": 0,
        "label_lines": 0,
        "instruction_lines": 0,
        "directive_lines": 0,
        "unclassified_lines": 0,
        "mnemonics": {},
        "directives": {},
        "pseudos": {},
        "relocations": {},
        "at_suffixes": {},
        "sections": {},
        "options": {},
        "cfi_directives": {},
        "expression_forms": {},
        "feature_occurrences": {},
    }


def counter_from(mapping: object) -> Counter[str]:
    if not isinstance(mapping, dict):
        return Counter()
    return Counter({str(k): int(v) for k, v in mapping.items()})


def sorted_counter(counter: Counter[str]) -> dict[str, int]:
    return dict(sorted(counter.items(), key=lambda item: (-item[1], item[0])))


def census_directory(root: Path, label: str) -> dict[str, object]:
    assembly_files = sorted(path for path in root.rglob("*.s") if path.is_file())
    totals_scalar = Counter()
    counters = {
        "mnemonics": Counter(),
        "directives": Counter(),
        "pseudos": Counter(),
        "relocations": Counter(),
        "at_suffixes": Counter(),
        "sections": Counter(),
        "options": Counter(),
        "cfi_directives": Counter(),
        "expression_forms": Counter(),
        "feature_occurrences": Counter(),
    }
    file_features: dict[str, list[str]] = {}
    files: dict[str, dict[str, int]] = {}
    examples: dict[str, list[dict[str, object]]] = {}

    started = time.monotonic()
    for file_index, path in enumerate(assembly_files, 1):
        if file_index == 1 or file_index % 50 == 0:
            elapsed = time.monotonic() - started
            print(
                f"MINIAS_CENSUS_PROGRESS label={label or 'unnamed'} "
                f"files={file_index - 1}/{len(assembly_files)} elapsed={elapsed:.3f}s",
                flush=True,
            )
        rel = path.relative_to(root).as_posix()
        file_key = f"{label}:{rel}" if label else rel
        features: set[str] = set()
        stats = Counter()
        raw_lines = path.read_text(encoding="utf-8", errors="surrogateescape").splitlines()
        stats["physical_lines"] = len(raw_lines)

        for line_no, raw_line in enumerate(raw_lines, 1):
            text = strip_comment(raw_line).strip()
            if not text:
                continue
            stats["nonempty_lines"] += 1

            # GAS permits labels before another statement on the same line.
            had_label = False
            while True:
                match = LABEL_RE.match(text)
                if match is None:
                    break
                had_label = True
                text = text[match.end():].lstrip()
            if had_label:
                stats["label_lines"] += 1
                if not text:
                    continue

            relocs = RELOC_RE.findall(text) if "%" in text else ()
            for reloc in relocs:
                feature = f"reloc:{reloc}"
                counters["relocations"][reloc] += 1
                counters["feature_occurrences"][feature] += 1
                features.add(feature)
                add_example(examples, feature, file_key, line_no, raw_line)

            suffixes = AT_SUFFIX_RE.findall(text) if "@" in text else ()
            for suffix in suffixes:
                feature = f"at:{suffix}"
                counters["at_suffixes"][suffix] += 1
                counters["feature_occurrences"][feature] += 1
                features.add(feature)
                add_example(examples, feature, file_key, line_no, raw_line)

            for feature in expression_features(text):
                name = feature.split(":", 1)[1]
                counters["expression_forms"][name] += 1
                counters["feature_occurrences"][feature] += 1
                features.add(feature)
                add_example(examples, feature, file_key, line_no, raw_line)

            directive_match = DIRECTIVE_RE.match(text)
            if directive_match is not None:
                directive = directive_match.group(1)
                args = directive_match.group(2).strip()
                stats["directive_lines"] += 1
                counters["directives"][directive] += 1
                feature = f"directive:{directive}"
                counters["feature_occurrences"][feature] += 1
                features.add(feature)
                add_example(examples, feature, file_key, line_no, raw_line)

                if directive in DIRECT_SECTION_DIRECTIVES:
                    section = directive
                    counters["sections"][section] += 1
                    section_feature = f"section-family:{section_family(section)}"
                    counters["feature_occurrences"][section_feature] += 1
                    features.add(section_feature)
                    add_example(examples, section_feature, file_key, line_no, raw_line)
                elif directive in SECTION_DIRECTIVES:
                    section = normalize_section_name(first_operand(args))
                    counters["sections"][section] += 1
                    section_feature = f"section-family:{section_family(section)}"
                    counters["feature_occurrences"][section_feature] += 1
                    features.add(section_feature)
                    add_example(examples, section_feature, file_key, line_no, raw_line)

                if directive == ".option":
                    option = first_operand(args).split()[0] if first_operand(args) else "<empty>"
                    counters["options"][option] += 1
                    option_feature = f"option:{option}"
                    counters["feature_occurrences"][option_feature] += 1
                    features.add(option_feature)
                    add_example(examples, option_feature, file_key, line_no, raw_line)

                if directive.startswith(".cfi_"):
                    counters["cfi_directives"][directive] += 1
                    cfi_feature = f"cfi:{directive}"
                    counters["feature_occurrences"][cfi_feature] += 1
                    features.add(cfi_feature)
                    add_example(examples, cfi_feature, file_key, line_no, raw_line)
                continue

            mnemonic_match = MNEMONIC_RE.match(text)
            if mnemonic_match is not None:
                mnemonic = mnemonic_match.group(1)
                stats["instruction_lines"] += 1
                counters["mnemonics"][mnemonic] += 1
                feature = f"insn:{mnemonic}"
                counters["feature_occurrences"][feature] += 1
                features.add(feature)
                add_example(examples, feature, file_key, line_no, raw_line)
                if mnemonic in KNOWN_PSEUDOS:
                    counters["pseudos"][mnemonic] += 1
                    pseudo_feature = f"pseudo:{mnemonic}"
                    counters["feature_occurrences"][pseudo_feature] += 1
                    features.add(pseudo_feature)
                    add_example(examples, pseudo_feature, file_key, line_no, raw_line)
                continue

            stats["unclassified_lines"] += 1
            feature = "syntax:unclassified"
            counters["feature_occurrences"][feature] += 1
            features.add(feature)
            add_example(examples, feature, file_key, line_no, raw_line)

        file_features[file_key] = sorted(features)
        files[file_key] = dict(sorted(stats.items()))
        totals_scalar.update(stats)

    elapsed = time.monotonic() - started
    print(
        f"MINIAS_CENSUS_SCAN_DONE label={label or 'unnamed'} "
        f"files={len(assembly_files)} elapsed={elapsed:.3f}s",
        flush=True,
    )

    totals = empty_totals()
    totals["files"] = len(assembly_files)
    for key in ("physical_lines", "nonempty_lines", "label_lines", "instruction_lines",
                "directive_lines", "unclassified_lines"):
        totals[key] = int(totals_scalar[key])
    for key, counter in counters.items():
        totals[key] = sorted_counter(counter)

    return {
        "schema": SCHEMA_VERSION,
        "label": label,
        "root": str(root),
        "totals": totals,
        "files": files,
        "file_features": file_features,
        "examples": examples,
        "sources": [{"label": label, "files": len(assembly_files)}],
    }


def merge_reports(paths: Iterable[Path], label: str) -> dict[str, object]:
    merged_scalar = Counter()
    merged_counters = {
        "mnemonics": Counter(),
        "directives": Counter(),
        "pseudos": Counter(),
        "relocations": Counter(),
        "at_suffixes": Counter(),
        "sections": Counter(),
        "options": Counter(),
        "cfi_directives": Counter(),
        "expression_forms": Counter(),
        "feature_occurrences": Counter(),
    }
    merged_files: dict[str, dict[str, int]] = {}
    merged_features: dict[str, list[str]] = {}
    merged_examples: dict[str, list[dict[str, object]]] = {}
    sources: list[dict[str, object]] = []

    for path in paths:
        report = json.loads(path.read_text())
        if report.get("schema") != SCHEMA_VERSION:
            raise SystemExit(f"unsupported census schema in {path}")
        totals = report["totals"]
        for key in ("files", "physical_lines", "nonempty_lines", "label_lines",
                    "instruction_lines", "directive_lines", "unclassified_lines"):
            merged_scalar[key] += int(totals.get(key, 0))
        for key in merged_counters:
            merged_counters[key].update(counter_from(totals.get(key, {})))

        for file_key, stats in report.get("files", {}).items():
            if file_key in merged_files:
                raise SystemExit(f"duplicate assembly file key while merging: {file_key}")
            merged_files[file_key] = stats
        for file_key, features in report.get("file_features", {}).items():
            merged_features[file_key] = list(features)
        for feature, examples in report.get("examples", {}).items():
            bucket = merged_examples.setdefault(feature, [])
            for example in examples:
                if len(bucket) >= EXAMPLE_LIMIT:
                    break
                bucket.append(example)
        sources.extend(report.get("sources", []))

    totals = empty_totals()
    for key in ("files", "physical_lines", "nonempty_lines", "label_lines",
                "instruction_lines", "directive_lines", "unclassified_lines"):
        totals[key] = int(merged_scalar[key])
    for key, counter in merged_counters.items():
        totals[key] = sorted_counter(counter)

    return {
        "schema": SCHEMA_VERSION,
        "label": label,
        "root": "<merged>",
        "totals": totals,
        "files": merged_files,
        "file_features": merged_features,
        "examples": merged_examples,
        "sources": sources,
    }


def greedy_cover(report: dict[str, object]) -> list[dict[str, object]]:
    by_file = {
        str(file_key): set(map(str, features))
        for file_key, features in report.get("file_features", {}).items()
    }
    uncovered: set[str] = set()
    for features in by_file.values():
        uncovered.update(features)

    selected: list[dict[str, object]] = []
    covered = 0
    total = len(uncovered)
    while uncovered:
        best_file = ""
        best_gain: set[str] = set()
        for file_key, features in by_file.items():
            gain = features & uncovered
            if len(gain) > len(best_gain) or (
                len(gain) == len(best_gain) and gain and file_key < best_file
            ):
                best_file = file_key
                best_gain = gain
        if not best_gain:
            break
        uncovered.difference_update(best_gain)
        covered += len(best_gain)
        selected.append({
            "file": best_file,
            "new_features": len(best_gain),
            "covered_features": covered,
            "total_features": total,
            "coverage_percent": round(100.0 * covered / total, 3) if total else 100.0,
        })
        by_file.pop(best_file, None)
    return selected


def render_counter(title: str, mapping: dict[str, int], limit: int | None = None) -> list[str]:
    rows = list(mapping.items())
    if limit is not None:
        rows = rows[:limit]
    out = [f"## {title}", "", "| Item | Count |", "| --- | ---: |"]
    if not rows:
        out.append("| _none_ | 0 |")
    else:
        out.extend(f"| `{name}` | {count} |" for name, count in rows)
    out.append("")
    return out


def render_summary(report: dict[str, object]) -> str:
    totals = report["totals"]
    cover = greedy_cover(report)
    lines = [
        f"# MiniAS assembly census — {report.get('label') or 'unnamed'}",
        "",
        f"- assembly files: **{totals['files']}**",
        f"- physical lines: **{totals['physical_lines']}**",
        f"- instruction lines: **{totals['instruction_lines']}**",
        f"- directive lines: **{totals['directive_lines']}**",
        f"- label lines: **{totals['label_lines']}**",
        f"- unclassified lines: **{totals['unclassified_lines']}**",
        f"- unique mnemonics: **{len(totals['mnemonics'])}**",
        f"- unique directives: **{len(totals['directives'])}**",
        f"- unique relocation operators: **{len(totals['relocations'])}**",
        f"- semantic feature keys: **{len(totals['feature_occurrences'])}**",
        "",
    ]
    lines += render_counter("Instruction mnemonics", totals["mnemonics"])
    lines += render_counter("Known pseudo/alias mnemonics", totals["pseudos"])
    lines += render_counter("Assembler directives", totals["directives"])
    lines += render_counter("Relocation operators", totals["relocations"])
    lines += render_counter("Sections", totals["sections"])
    lines += render_counter(".option values", totals["options"])
    lines += render_counter("CFI directives", totals["cfi_directives"])
    lines += render_counter("Expression forms", totals["expression_forms"])

    rare = [
        (feature, count)
        for feature, count in totals["feature_occurrences"].items()
        if count <= 5
    ]
    rare.sort(key=lambda item: (item[1], item[0]))
    lines += ["## Rare features (<=5 occurrences)", "", "| Feature | Count |", "| --- | ---: |"]
    if rare:
        lines += [f"| `{feature}` | {count} |" for feature, count in rare]
    else:
        lines.append("| _none_ | 0 |")
    lines.append("")

    lines += [
        "## Greedy feature-cover corpus",
        "",
        "This is a deterministic set-cover heuristic over mnemonics, directives,",
        "relocation operators, sections, options, CFI forms, pseudo aliases and",
        "expression-shape features. It is intended to seed fast MiniAS gates;",
        "the full frozen corpus remains the promotion gate.",
        "",
        "| # | File | New features | Cumulative coverage |",
        "| ---: | --- | ---: | ---: |",
    ]
    for index, row in enumerate(cover, 1):
        lines.append(
            f"| {index} | `{row['file']}` | {row['new_features']} | "
            f"{row['coverage_percent']:.3f}% |"
        )
    if not cover:
        lines.append("| 0 | _none_ | 0 | 100.000% |")
    lines.append("")
    return "\n".join(lines)


def write_cover(path: Path, report: dict[str, object]) -> None:
    cover = greedy_cover(report)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "".join(
            f"{row['file']}\t{row['new_features']}\t{row['coverage_percent']:.3f}\n"
            for row in cover
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--input", type=Path)
    mode.add_argument("--merge", nargs="+", type=Path)
    parser.add_argument("--label", default="")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--cover-out", type=Path)
    args = parser.parse_args()

    if args.input is not None:
        if not args.input.is_dir():
            raise SystemExit(f"assembly input directory does not exist: {args.input}")
        report = census_directory(args.input, args.label)
    else:
        assert args.merge is not None
        report = merge_reports(args.merge, args.label or "merged")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if args.summary is not None:
        args.summary.parent.mkdir(parents=True, exist_ok=True)
        args.summary.write_text(render_summary(report))
    if args.cover_out is not None:
        write_cover(args.cover_out, report)

    totals = report["totals"]
    print(
        "MINIAS_CENSUS"
        f" label={report.get('label') or 'unnamed'}"
        f" files={totals['files']}"
        f" mnemonics={len(totals['mnemonics'])}"
        f" directives={len(totals['directives'])}"
        f" relocations={len(totals['relocations'])}"
        f" features={len(totals['feature_occurrences'])}"
        f" unclassified={totals['unclassified_lines']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
