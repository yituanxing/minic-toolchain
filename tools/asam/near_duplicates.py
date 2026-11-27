#!/usr/bin/env python3
"""ASAM v1 second-pass STRUCTURAL CANDIDATES (not semantic proof).

Uses Ctags function ranges, normalized lexical shapes and shingles.
This is deliberately a read-only candidate generator.
"""
import argparse
import collections
import hashlib
import itertools
import json
from pathlib import Path
import re
import zlib

COMMENTS = re.compile(rb"/\*.*?\*/|//[^\n]*", re.S)
LITERALS = re.compile(rb'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
TOKEN = re.compile(rb"[A-Za-z_][A-Za-z_0-9]*|0[xX][0-9A-Fa-f]+|[0-9]+|==|!=|<=|>=|&&|\|\||<<|>>|->|[^\s]")
KEYWORDS = {
    "if", "else", "for", "while", "do", "return", "switch", "case", "default",
    "break", "continue", "goto", "sizeof", "struct", "union", "enum", "typedef",
    "static", "extern", "inline", "const", "volatile", "restrict", "void",
    "int", "long", "short", "char", "unsigned", "signed", "float", "double",
    "_Bool", "_Atomic", "_Alignof", "_Static_assert", "bool", "size_t"
}

def tokens(data):
    data = LITERALS.sub(b" STRING ", COMMENTS.sub(b" ", data))
    output = []
    for match in TOKEN.findall(data):
        word = match.decode("utf-8", "replace")
        if re.match(r"^[A-Za-z_][A-Za-z_0-9]*$", word):
            output.append(word if word in KEYWORDS else "ID")
        elif re.match(r"^(0[xX][0-9A-Fa-f]+|[0-9]+)$", word):
            output.append("NUM")
        else:
            output.append(word)
    return output

def extract(symbols, root):
    per_file = {}
    entries = []
    for s in symbols:
        start, end = s["line"], s["end"]
        if not start or not end or end <= start or end - start < 9:
            continue
        path = root / s["path"]
        if path not in per_file:
            per_file[path] = path.read_bytes().splitlines(keepends=True)
        lines = per_file[path]
        if end > len(lines):
            continue
        body = b"".join(lines[start - 1:end])
        first_brace = body.find(b"{")
        if first_brace < 0:
            continue
        shape = tokens(body[first_brace:])
        if not 70 <= len(shape) <= 3000:
            continue
        shingles = {
            zlib.crc32("\0".join(shape[i:i + 7]).encode("utf-8"))
            for i in range(len(shape) - 6)
        }
        if len(shingles) < 20:
            continue
        entries.append({
            "name": s["name"], "path": s["path"], "line": start,
            "end": end, "token_count": len(shape), "shingles": shingles
        })
    return entries

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--repo", default=".")
    p.add_argument("--input", default="asam-out/symbols.json")
    p.add_argument("--output", default="asam-out")
    args = p.parse_args()
    root = Path(args.repo).resolve()
    src = (root / args.input).resolve()
    dest = (root / args.output).resolve()
    for path in (src, dest):
        if path != root and root not in path.parents:
            p.error("all paths must stay in checkout")
    items = extract(json.loads(src.read_text(encoding="utf-8")), root)
    buckets = collections.defaultdict(list)
    for i, entry in enumerate(items):
        # Lowest hashes give a cheap, deterministic candidate filter.
        for h in sorted(entry["shingles"])[:12]:
            buckets[h].append(i)
    pairs = set()
    for ids in buckets.values():
        if 1 < len(ids) <= 60:
            pairs.update(itertools.combinations(ids, 2))
    findings = []
    for a, b in pairs:
        x, y = items[a], items[b]
        if (x["path"], x["line"]) == (y["path"], y["line"]):
            continue
        ratio = min(x["token_count"], y["token_count"]) / max(x["token_count"], y["token_count"])
        if ratio < 0.72:
            continue
        intersection = len(x["shingles"] & y["shingles"])
        union = len(x["shingles"]) + len(y["shingles"]) - intersection
        score = intersection / union if union else 0
        if score < 0.56:
            continue
        clean = lambda item: {key: item[key]
                              for key in ("name", "path", "line", "end", "token_count")}
        findings.append({"score": round(score, 4),
                         "kind": "normalized-lexical-shingle-similarity",
                         "a": clean(x), "b": clean(y)})
    findings.sort(key=lambda e: (-e["score"], e["a"]["path"], e["a"]["line"]))
    findings = findings[:100]
    dest.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": "asam-v1-structural-1",
        "eligible_functions": len(items),
        "raw_candidate_pairs": len(pairs),
        "ranked_pairs": findings,
        "warning": "These are name-normalized lexical similarities, not semantic duplicates. "
                   "No code modification is authorized from this result."
    }
    (dest / "near-duplicates.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    lines = ["# ASAM v1 structural-similarity candidates", "",
             "Eligible functions: " + str(len(items)),
             "Pairs checked: " + str(len(pairs)),
             "Pairs over threshold (top 100): " + str(len(findings)), "",
             "Every pair below requires independent semantic and caller analysis.", ""]
    for item in findings[:50]:
        a, b = item["a"], item["b"]
        lines.append("- score " + str(item["score"]) + ": " + a["path"] + ":" +
                     str(a["line"]) + " " + a["name"] + "  <->  " + b["path"] +
                     ":" + str(b["line"]) + " " + b["name"])
    (dest / "near-duplicates.md").write_text("\n".join(lines) + "\n",
                                            encoding="utf-8")
    print("ASAM structural eligible functions:", len(items))
    print("ASAM structural candidate pairs:", len(pairs))
    print("ASAM ranked similarities:", len(findings))
    for item in findings[:8]:
        print(str(item["score"]), item["a"]["name"], "<->", item["b"]["name"])

if __name__ == "__main__":
    main()
