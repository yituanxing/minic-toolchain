#!/usr/bin/env python3
"""Selftests for semantic-anchor ranking and bounded Scout handoff."""
from collections import Counter
import json
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import near_duplicates as nd
import review_packets as rp

shape, shingles, anchors = nd.extract_features(
    b'{ if (a) return foo(obj->field, MINIC_TEST_FLAG); return 0; }')
assert "call:foo" in anchors
assert "field:field" in anchors
assert "constant:MINIC_TEST_FLAG" in anchors
assert len(shape) > 5

def sample(name, anchors, offset):
    return {
        "name": name, "path": "src/example.c", "line": offset,
        "end": offset + 15, "tokens": 200, "shingles": set(range(80)),
        "anchors": Counter(anchors)
    }
items = [
    sample("same_a", {"call:foo": 2, "constant:MINIC_F": 2, "field:val": 1}, 1),
    sample("same_b", {"call:foo": 2, "constant:MINIC_F": 2, "field:val": 1}, 20),
    sample("unrelated", {"call:other": 2, "constant:NOT_RELATED": 2}, 40),
]
checked, all_found, selected, pattern_only = nd.rank(items)
assert checked == 3, checked
assert any({x["a"]["name"], x["b"]["name"]} == {"same_a", "same_b"}
           for x in selected), selected
assert all("unrelated" not in (x["a"]["name"], x["b"]["name"])
           for x in selected), selected
assert pattern_only, "structural-only pair must remain in separate pattern queue"

with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    src = root / "src" / "example.c"
    src.parent.mkdir()
    lines = []
    for i in range(120):
        lines.append("int line_%d = %d;" % (i, i))
    src.write_text("\n".join(lines) + "\n", encoding="utf-8")
    packet = {
        "kind": "shared-implementation-candidate", "score": 0.91,
        "shape": 0.97, "semantic_anchor_overlap": 0.86,
        "shared_calls": ["foo"], "shared_fields": ["val"],
        "distinct_literal_sets": False,
        "a": {"name": "same_a", "path": "src/example.c", "line": 1, "end": 16},
        "b": {"name": "same_b", "path": "src/example.c", "line": 20, "end": 35}
    }
    result = rp.build(root, [packet], limit=2, max_source_lines=12)
    assert result["candidate_count"] == 1
    p = result["packets"][0]
    assert p["a"]["truncated"]
    assert p["a"]["shown_through"] == 12
    assert p["review_status"] == "NEEDS_SEMANTIC_EVIDENCE"
    assert p["id"].startswith("ASAM-")
    assert len(json.dumps(result)) < 12000

print("ASAM v1.1 PASS: anchor-aware ranking, separate pattern queue, bounded packets")
