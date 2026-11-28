#!/usr/bin/env python3
"""ASAM v1 read-only C source inventory and candidate generator.

A candidate is never proof of dead code. This program never edits production files.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

TOKEN = re.compile(rb"[A-Za-z_][A-Za-z_0-9]*")
COMMENT = re.compile(rb"/\*.*?\*/|//[^\n]*", re.S)

def sources(root):
    return sorted(p for p in root.rglob("*")
                  if p.is_file() and p.suffix in {".c", ".h"})

def contained(root, other):
    return other == root or root in other.parents

def tags(files, root):
    argv = ["ctags", "--output-format=json", "--languages=C", "--kinds-C=f",
            "--fields=+neK", "-o", "-", "-L", "-"]
    names = [str(p.relative_to(root)) for p in files]
    proc = subprocess.run(argv, input="\n".join(names) + "\n",
                          cwd=root, text=True, capture_output=True,
                          timeout=180, check=False)
    if proc.returncode:
        raise RuntimeError("ctags failed: " + proc.stderr[-2000:])
    result = []
    for line in proc.stdout.splitlines():
        if not line.startswith("{"):
            continue
        item = json.loads(line)
        if item.get("_type") != "tag" or item.get("kind") not in ("function", "f"):
            continue
        if not item.get("name") or not item.get("path"):
            continue
        result.append({
            "name": item["name"], "path": item["path"],
            "line": int(item.get("line") or 0),
            "end": int(item["end"]) if item.get("end") else None,
            "file_local": bool(item.get("file", False)),
        })
    return sorted(result, key=lambda x: (x["path"], x["line"], x["name"]))

def token_counts(paths):
    counts = collections.Counter()
    for path in paths:
        counts.update(word.decode("ascii") for word in TOKEN.findall(path.read_bytes()))
    return counts

def exact_groups(symbols, root):
    lines_by_path = {}
    groups = collections.defaultdict(list)
    for s in symbols:
        start, end = s["line"], s["end"]
        if not start or not end or end - start < 5:
            continue
        path = root / s["path"]
        if path not in lines_by_path:
            lines_by_path[path] = path.read_bytes().splitlines(keepends=True)
        lines = lines_by_path[path]
        if end > len(lines):
            continue
        body = b"".join(lines[start - 1:end])
        key_data = re.sub(rb"\s+", b"", COMMENT.sub(b"", body))
        if len(key_data) < 180:
            continue
        digest = hashlib.sha256(key_data).hexdigest()
        groups[digest].append({key: s[key] for key in ("name", "path", "line", "end")})
    return [{"hash": digest, "definitions": definitions}
            for digest, definitions in sorted(groups.items()) if len(definitions) > 1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=".")
    parser.add_argument("--scope", default="src/target/x86_64")
    parser.add_argument("--output", default="asam-out")
    args = parser.parse_args()
    root = Path(args.repo).resolve()
    scope = (root / args.scope).resolve()
    if not contained(root, scope) or not scope.is_dir():
        parser.error("scope must be a directory within the repository")
    files = sources(scope)
    if not files:
        parser.error("no C/H sources in scope")
    global_roots = [root / s for s in ("src", "tools/minic", "tools/minic-cc")]
    global_files = sorted(set(p for r in global_roots if r.is_dir() for p in sources(r)))
    counts = token_counts(global_files)
    symbols = tags(files, root)
    unreferenced = [s for s in symbols if s["file_local"] and counts[s["name"]] <= 1]
    exact = exact_groups(symbols, root)
    total_lines = sum(len(p.read_bytes().splitlines()) for p in files)
    report = {
        "schema": "asam-v1-scan-1", "sha": os.environ.get("GITHUB_SHA", "local"),
        "scope": str(scope.relative_to(root)), "scope_files": len(files),
        "scope_lines": total_lines, "functions": len(symbols),
        "global_reference_files": len(global_files),
        "compile_database_present": (root / "compile_commands.json").exists(),
        "unreferenced_static_candidates": unreferenced,
        "lexical_duplicate_groups": exact,
        "warnings": [
            "These are candidates, NOT confirmed defects or dead code.",
            "Raw token counts do not resolve build variants, registrations, function pointers or ASM.",
            "Ctags alone does not provide an exhaustive call graph.",
            "No production code was changed and no regression tests were run."
        ]
    }
    dest = (root / args.output).resolve()
    if not contained(root, dest):
        parser.error("output must remain within repository")
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (dest / "symbols.json").write_text(
        json.dumps(symbols, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    rows = [
        "# ASAM v1 read-only scan", "",
        "- Commit: " + report["sha"],
        "- Scope: " + report["scope"],
        "- C/H files: " + str(len(files)),
        "- Physical lines: " + str(total_lines),
        "- Functions indexed: " + str(len(symbols)),
        "- Global files searched for token references: " + str(len(global_files)),
        "- Unreferenced-static candidates: " + str(len(unreferenced)),
        "- Lexical-duplicate groups: " + str(len(exact)), "",
        "## Unreferenced-static candidates (NOT proven dead)", ""
    ]
    rows += ["- " + s["path"] + ":" + str(s["line"]) + " " + s["name"]
             for s in unreferenced[:80]]
    rows += ["", "## Exact lexical duplicates", ""]
    rows += ["- " + ", ".join(x["path"] + ":" + str(x["line"]) + " " + x["name"]
                             for x in g["definitions"]) for g in exact[:40]]
    rows += ["", "## Scope and limitations", "",
             "This scan is evidence collection, not a source-editing operation.",
             "Unresolved indirect calls, platform configuration and non-C references",
             "must be checked before any deletion. Compilation/tests are separate gates.", ""]
    (dest / "report.md").write_text("\n".join(rows), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items()
                      if k in {"sha", "scope", "scope_files", "scope_lines",
                               "functions", "global_reference_files"}},
                     ensure_ascii=False, indent=2))
    print("Candidate functions:", len(unreferenced))
    print("Exact groups:", len(exact))
    return 0

if __name__ == "__main__":
    sys.exit(main())
