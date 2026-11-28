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
assert len(a["slice_candidates"]) == len(state["slices"]) == 10
assert a["tracked_c_h_files"] >= 200
assert a["repository_total_tracked_c_h"] >= a["tracked_c_h_files"]
assert a["repository_outside_scan_roots_count"] == len(
    a["repository_outside_scan_roots"]
)
# The old inventory omitted 153 in-scope C/H files. Full repo contains
# separately owned external tools/libraries; classify every external root
# rather than claiming those files are MiniC production implementation.
assert a["tracked_c_h_files"] == 1899
assert a["repository_total_tracked_c_h"] == 1983
assert a["repository_excluded_external_c_h_count"] == 84
assert a["repository_unclassified_c_h_count"] == 0
assert a["repository_outside_scan_roots"] == a["repository_excluded_external_c_h"]
assert "include/minic/compiler.h" not in a["repository_outside_scan_roots"]
assert "tests/driver/staged-target-forward-test.c" not in a["repository_outside_scan_roots"]
assert all(name.startswith(tuple(a["explicit_external_roots"]))
           for name in a["repository_outside_scan_roots"])
assert a["tracked_physical_lines"] > 40000
assert a["unique_candidate_files"] <= a["tracked_c_h_files"]
assert a["unassigned_count"] == 0, a["unassigned_files"]
assert next(x["tracked_file_candidates"] for x in a["slice_candidates"]
            if x["id"] == "C10") >= 5
assert next(x["tracked_file_candidates"] for x in a["slice_candidates"]
            if x["id"] == "C01") >= 12
assert a["unassigned_count"] + a["unique_candidate_files"] == a["tracked_c_h_files"]
# P0 scope attribution regression: the original C07 advertised a nonexistent
# riscv/ directory. C06 also omitted the *actual* source-policy/layout owner.
# Paths are actual pinned tracked C/H, not invented lexical hints.
candidate_paths = {entry["path"] for entry in a["source_file_fingerprints"]}
for required in (
    "src/target/source_policy/language_semantics.c",
    "src/target/source_policy/data_layout_source.c",
    "src/target/common/core_function_plan.c",
    "src/target/riscv32/rv32_emit.c",
    "src/target/riscv64/codegen_function.c",
):
    assert required in candidate_paths, required
c06 = next(s for s in a["slice_candidates"] if s["id"] == "C06")
c07 = next(s for s in a["slice_candidates"] if s["id"] == "C07")
assert "src/target/source_policy/" in c06["scope_hint"]
assert "src/target/riscv32/" in c07["scope_hint"]
assert "src/target/riscv64/" in c07["scope_hint"]
assert "src/target/riscv/" not in c07["scope_hint"]
assert c06["tracked_file_candidates"] >= 5
assert c07["tracked_file_candidates"] >= 5
assert state["recorded_events"][8] == sup.EXPECTED_SCOPE_REFINEMENT_V3
assert all(not s["claimed_semantic_coverage"] for s in a["slice_candidates"])
assert all(s["supervisor_status"] in ("PLANNED", "MAPPING_PROPOSED")
           for s in a["slice_candidates"])
assert sum(s["supervisor_status"] == "MAPPING_PROPOSED"
           for s in a["slice_candidates"]) == sum(
               x["status"] == "MAPPING_PROPOSED" for x in state["slices"])
assert sum(s["supervisor_status"] == "PLANNED"
           for s in a["slice_candidates"]) == sum(
               x["status"] == "PLANNED" for x in state["slices"])
assert set(a["canonical_documents"]) == set(policy["authoritative_sources"])

def hints(slice_id):
    return next(x for x in a["slice_candidates"] if x["id"] == slice_id)["anchor_hints"]

assert any(x.get("matched_locations", 0) > 0 for x in hints("C01")), "missing live compiler entry"
assert next(x["matched_locations"] for x in hints("C01")
            if x["symbol"] == "main") < 30, "test fixture mains polluted production anchors"
assert any(x.get("matched_locations", 0) > 0 for x in hints("C02")), "missing frontend parse entry"
# A historic name may have migrated: preserve it as an explicit drift clue
# instead of inventing coverage or requiring stale source to exist.
assert a["unresolved_documented_anchor_count"] >= 0
assert all(x["status"] in ("LEXICAL_LOCATION_NOT_PROOF",
                           "NOT_FOUND_REQUIRES_MAPPING",
                           "make_target_outside_C_H_scan")
           for slice_ in a["slice_candidates"] for x in slice_["anchor_hints"])
assert sum(len(s["unresolved_documented_anchors"])
           for s in a["slice_candidates"]) == a["unresolved_documented_anchor_count"]
assert a["slice_candidates"][4]["id"] == "C05"
assert "minic_prune_inline_specializations_from_core" in (
    [x["symbol"] for x in hints("C05")]
), "must keep the canonical historic entrance to investigate"


# A stale source revision must fail; never silently attribute coverage to new code.
wrong = copy.deepcopy(state)
wrong["source_snapshot"]["observed_commit"] = "f" * 40
try:
    topo.map_slices(root, policy, wrong)
except ValueError:
    pass
else:
    raise AssertionError("stale baseline accepted as current")

# A modified working-tree source must not be silently indexed as pristine
# origin/main. Restore its exact bytes even when this test fails.
tracked_path = root / a["source_file_fingerprints"][0]["path"]
original_bytes = tracked_path.read_bytes()
try:
    tracked_path.write_bytes(original_bytes + b"\\n/* dirty ASAM test fixture */\\n")
    try:
        topo.map_slices(root, policy, state)
    except ValueError as exc:
        assert "dirty" in str(exc), str(exc)
    else:
        raise AssertionError("working-tree source poisoning was accepted")
finally:
    tracked_path.write_bytes(original_bytes)

# Source code fingerprints must remain distinct from architecture/issue facts.
assert all(len(x["sha256"]) == 64 and x["path"].endswith((".c", ".h"))
           for x in a["source_file_fingerprints"])
assert all(not x["verified"] and x["last_verified_revision"] is None
           for x in state["slices"])
assert state["slices"][0]["canonical_evidence_refs"]
assert all(not x["canonical_evidence_refs"]
           for x in state["slices"] if x["status"] == "PLANNED")
assert all(x["canonical_evidence_refs"]
           for x in state["slices"] if x["status"] == "MAPPING_PROPOSED")

print("ASAM_BREADTH_INVENTORY=PASS full tracked sources and ten provisional function regions")
print("ASAM_FALSE_COVERAGE_GUARD=PASS lexical discovery never implies semantic completion")
print("ASAM_STALE_SOURCE_GUARD=PASS rejects revised main without S0 rebaseline")
