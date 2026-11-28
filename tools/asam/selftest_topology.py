#!/usr/bin/env python3
"""No-model safety test for whole-repo breadth and task handoff artifacts."""
import copy
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import supervisor as sup
import topology as topo

root = Path(__file__).resolve().parents[2]
policy, state = sup.load(root)
a = topo.map_slices(root, policy, state)
assert a["mission_id"] == policy["mission_id"]
assert a["semantic_reviewed"] == 0
assert len(a["slice_candidates"]) == len(state["slices"]) == 9
assert a["tracked_c_h_files"] >= 200
assert a["tracked_physical_lines"] > 40000
assert a["unique_candidate_files"] <= a["tracked_c_h_files"]
assert a["unassigned_count"] + a["unique_candidate_files"] == a["tracked_c_h_files"]
assert all(not s["claimed_semantic_coverage"] for s in a["slice_candidates"])
assert all(s["supervisor_status"] == "PLANNED" for s in a["slice_candidates"])
assert set(a["canonical_documents"]) == set(policy["authoritative_sources"])

def hints(slice_id):
    return next(x for x in a["slice_candidates"] if x["id"] == slice_id)["anchor_hints"]

assert any(x.get("matched_locations", 0) > 0 for x in hints("C01")), "missing live compiler entry"
assert any(x.get("matched_locations", 0) > 0 for x in hints("C02")), "missing frontend parse entry"
assert any(x.get("matched_locations", 0) > 0 for x in hints("C05")), "missing core pruning entry"
assert any(x.get("matched_locations", 0) > 0 for x in hints("C08")), "missing output transaction entry"

# A stale source revision must fail; never silently attribute coverage to new code.
wrong = copy.deepcopy(state)
wrong["source_snapshot"]["observed_commit"] = "f" * 40
try:
    topo.map_slices(root, policy, wrong)
except ValueError:
    pass
else:
    raise AssertionError("stale baseline accepted as current")

# Source code fingerprints must remain distinct from architecture/issue facts.
assert all(len(x["sha256"]) == 64 and x["path"].endswith((".c", ".h"))
           for x in a["source_file_fingerprints"])
assert all(not x["canonical_evidence_refs"] and x["status"] == "PLANNED"
           for x in state["slices"])

print("ASAM_BREADTH_INVENTORY=PASS full tracked sources and nine provisional function regions")
print("ASAM_FALSE_COVERAGE_GUARD=PASS lexical discovery never implies semantic completion")
print("ASAM_STALE_SOURCE_GUARD=PASS rejects revised main without S0 rebaseline")
