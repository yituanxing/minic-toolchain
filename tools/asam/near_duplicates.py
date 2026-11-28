#!/usr/bin/env python3
"""ASAM structural similarity v2.

Heuristic discovery only. Separates repeated algorithm *shape* from shared
semantic anchors (callees, fields, enum symbols, literals). Neither is proof
that two implementations can be merged or deleted.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import itertools
import json
import math
from pathlib import Path
import re
import zlib

LEX = re.compile(
    rb'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'
    rb"'(?:\\.|[^'\\])*'|"
    rb'0[xX][0-9a-fA-F]+|\d+|[A-Za-z_][A-Za-z_0-9]*|'
    rb'==|!=|<=|>=|&&|\|\||<<|>>|->|[^\s]', re.S
)
IDENT = re.compile(r'^[A-Za-z_][A-Za-z_0-9]*$')
NUMBER = re.compile(r'^(0[xX][0-9a-fA-F]+|\d+)$')
KEYWORDS = set(("if else for while do return switch case default break continue "
                "goto sizeof struct union enum typedef static extern inline "
                "const volatile restrict void int long short char unsigned "
                "signed float double _Bool _Atomic _Alignof _Static_assert "
                "bool size_t _Generic _Alignas").split())
CONTROL = {"if", "for", "while", "switch", "sizeof", "_Generic"}
SHINGLE_SIZE = 7

def extract_features(body):
    raw = []
    for match in LEX.findall(body):
        if match.startswith(b"/*") or match.startswith(b"//"):
            continue
        raw.append(match.decode("utf-8", "replace"))
    shape, anchors = [], Counter()
    for index, word in enumerate(raw):
        if IDENT.match(word):
            shape.append(word if word in KEYWORDS else "ID")
            if word not in KEYWORDS:
                next_word = raw[index + 1] if index + 1 < len(raw) else ""
                prev_word = raw[index - 1] if index else ""
                if next_word == "(" and word not in CONTROL:
                    anchors["call:" + word] += 1
                elif prev_word in (".", "->"):
                    anchors["field:" + word] += 1
                elif len(word) > 2 and (word.isupper() or word.startswith(("MINIC_", "X86_", "RV_"))):
                    anchors["constant:" + word] += 1
        elif NUMBER.match(word):
            shape.append("NUM")
            if word not in ("0", "0U", "1", "1U"):  # literal can change behavior
                anchors["number:" + word] += 1
        elif word[0] in ("'", '"'):
            shape.append("LITERAL")
            anchors["literal:" + hashlib.sha1(word.encode()).hexdigest()[:16]] += 1
        else:
            shape.append(word)
    shingles = {zlib.crc32("\x00".join(shape[i:i + SHINGLE_SIZE]).encode())
                for i in range(len(shape) - SHINGLE_SIZE + 1)}
    return shape, shingles, anchors

def functions(symbols, root):
    source_cache = {}
    entries = []
    for s in symbols:
        start, end = s["line"], s["end"]
        if not start or not end or end - start < 9:
            continue
        path = root / s["path"]
        if path not in source_cache:
            source_cache[path] = path.read_bytes().splitlines(keepends=True)
        lines = source_cache[path]
        if end > len(lines):
            continue
        body = b"".join(lines[start - 1:end])
        opening = body.find(b"{")
        if opening < 0:
            continue
        shape, shingles, anchors = extract_features(body[opening:])
        if len(shape) < 70 or len(shape) > 3500 or len(shingles) < 18:
            continue
        entries.append({
            "name": s["name"], "path": s["path"], "line": start,
            "end": end, "tokens": len(shape), "shingles": shingles,
            "anchors": anchors
        })
    return entries

def jaccard(a, b):
    intersection = len(a & b)
    union = len(a) + len(b) - intersection
    return intersection / union if union else 0.0

def cosine_idf(a, b, frequencies, n):
    keys = set(a) | set(b)
    if not keys:
        return 0.0
    common = set(a) & set(b)
    weights = {key: math.log(1.0 + (n + 1) / (frequencies[key] + 1))
               for key in keys}
    numerator = sum(a[key] * b[key] * weights[key] ** 2 for key in common)
    norm_a = sum((count * weights[k]) ** 2 for k, count in a.items())
    norm_b = sum((count * weights[k]) ** 2 for k, count in b.items())
    return numerator / (math.sqrt(norm_a * norm_b)) if norm_a and norm_b else 0.0

def normalize(entry):
    return {k: entry[k] for k in ("name", "path", "line", "end", "tokens")}

def rank(items):
    freqs = Counter()
    for item in items:
        freqs.update(item["anchors"])
    # Min-hash-style candidate retrieval; cap giant repeated-template buckets.
    buckets = defaultdict(list)
    for i, item in enumerate(items):
        for h in sorted(item["shingles"])[:10]:
            buckets["s", h].append(i)
        unique_anchors = sorted(
            (anchor for anchor in item["anchors"]
             if freqs[anchor] <= max(35, len(items) // 6)),
            key=lambda s: (freqs[s], s)
        )[:5]
        for anchor in unique_anchors:
            buckets["a", anchor].append(i)
    pairs = set()
    for ids in buckets.values():
        if 1 < len(ids) <= 55:
            pairs.update(itertools.combinations(sorted(set(ids)), 2))
    findings, pattern_only = [], []
    for a, b in sorted(pairs):
        x, y = items[a], items[b]
        if min(x["tokens"], y["tokens"]) / max(x["tokens"], y["tokens"]) < 0.65:
            continue
        shape = jaccard(x["shingles"], y["shingles"])
        if shape < 0.57:
            continue
        common = x["anchors"].keys() & y["anchors"].keys()
        anchor = cosine_idf(x["anchors"], y["anchors"], freqs, len(items))
        calls = sorted(k[5:] for k in common if k.startswith("call:"))
        fields = sorted(k[6:] for k in common if k.startswith("field:"))
        differing_literals = bool(
            {k for k in x["anchors"] if k.startswith("literal:")} ^
            {k for k in y["anchors"] if k.startswith("literal:")}
        )
        score = 0.46 * shape + 0.54 * anchor
        record = {
            "score": round(score, 4),
            "shape": round(shape, 4),
            "semantic_anchor_overlap": round(anchor, 4),
            "shared_calls": calls[:10], "shared_fields": fields[:10],
            "distinct_literal_sets": differing_literals,
            "a": normalize(x), "b": normalize(y),
        }
        # Similar syntax with different called APIs / architecture constants
        # is a *pattern*, not a promising deduplication recommendation.
        if anchor >= 0.45 and len(common) >= 2 and score >= 0.65:
            record["kind"] = "shared-implementation-candidate"
            findings.append(record)
        elif shape >= 0.79:
            record["kind"] = "pattern-only-candidate"
            pattern_only.append(record)
    findings.sort(key=lambda x: (-x["score"], x["a"]["path"],
                                 x["a"]["line"], x["b"]["line"]))
    pattern_only.sort(key=lambda x: (-x["shape"], x["a"]["path"],
                                     x["a"]["line"], x["b"]["line"]))
    # Diversify the queue: one common wrapper should not consume every slot.
    seen = Counter()
    selected = []
    for item in findings:
        key_a = (item["a"]["path"], item["a"]["line"])
        key_b = (item["b"]["path"], item["b"]["line"])
        if seen[key_a] >= 3 or seen[key_b] >= 3:
            continue
        selected.append(item)
        seen[key_a] += 1
        seen[key_b] += 1
        if len(selected) == 80:
            break
    return len(pairs), findings, selected, pattern_only

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--repo", default=".")
    p.add_argument("--input", default="asam-out/symbols.json")
    p.add_argument("--output", default="asam-out")
    args = p.parse_args()
    root = Path(args.repo).resolve()
    src, dest = (root / args.input).resolve(), (root / args.output).resolve()
    if not all(x == root or root in x.parents for x in (src, dest)):
        p.error("input and output must remain in repository")
    symbols = json.loads(src.read_text(encoding="utf-8"))
    items = functions(symbols, root)
    pairs, all_findings, selected, patterns = rank(items)
    output = {
        "schema": "asam-v1-structural-2",
        "eligible_functions": len(items),
        "pairs_considered": pairs,
        "shared_implementation_candidates": len(all_findings),
        "cross_file_candidates": sum(1 for x in all_findings if x["a"]["path"] != x["b"]["path"]),
        "pattern_only_candidates": len(patterns),
        "ranked_pairs": selected,
        "pattern_examples": patterns[:12],
        "warning": "Lexical/anchor similarities are hypotheses, never verified "
                   "semantic duplicates or permission to modify code."
    }
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "near-duplicates.json").write_text(
        json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    lines = ["# ASAM v1.1 structural candidate ranking", "",
             "- Eligible functions: " + str(len(items)),
             "- Compared candidate pairs: " + str(pairs),
             "- Shared-implementation hypotheses: " + str(len(all_findings)),
             "- Cross-file hypotheses: " + str(sum(1 for x in all_findings if x["a"]["path"] != x["b"]["path"])),
             "- Pattern-only (separate queue): " + str(len(patterns)),
             "- Diverse main queue: " + str(len(selected)), "",
             "Structural similarity is not equivalence. Check callers, effects,",
             "return contracts and required ISA/ABI differences before any edit.", "",
             "## Shared-implementation hypotheses", ""]
    for item in selected[:40]:
        a, b = item["a"], item["b"]
        lines.append("- score " + str(item["score"]) +
                     " (shape " + str(item["shape"]) +
                     ", anchors " + str(item["semantic_anchor_overlap"]) +
                     "): " + a["path"] + ":" + str(a["line"]) + " " +
                     a["name"] + " <-> " + b["path"] + ":" +
                     str(b["line"]) + " " + b["name"])
    lines += ["", "## Pattern-only examples (not safe to combine)", ""]
    for item in patterns[:12]:
        lines.append("- " + item["a"]["name"] + " <-> " + item["b"]["name"] +
                     ", shape " + str(item["shape"]) +
                     ", anchors " + str(item["semantic_anchor_overlap"]))
    (dest / "near-duplicates.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("ASAM v1.1 eligible:", len(items))
    print("ASAM v1.1 compared pairs:", pairs)
    print("ASAM v1.1 shared-implementation hypotheses:", len(all_findings))
    print("ASAM v1.1 pattern-only hypotheses:", len(patterns))
    print("ASAM v1.1 cross-file hypotheses:", sum(1 for x in all_findings if x["a"]["path"] != x["b"]["path"]))
    print("ASAM v1.1 diverse queue:", len(selected))
    for item in selected[:8]:
        print(item["score"], item["a"]["name"], "<->", item["b"]["name"])

if __name__ == "__main__":
    main()
