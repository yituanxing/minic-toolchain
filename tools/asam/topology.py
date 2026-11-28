#!/usr/bin/env python3
"""Read-only, breadth-first MiniC source inventory for a context-free ASAM handoff.

The output is NOT an audited functional graph: it distinguishes inventory
coverage, lexical evidence, and semantic review. All semantic authority stays
in existing MiniC architecture, contracts and issue registers.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

C_SUFFIXES = {".c", ".h"}
SCAN_ROOTS = ("src/", "tools/minic/", "tools/minic-cc/",
              "tools/minic-", "include/minic/", "tests/")
# These belong to distinct tools or vendored libraries, not the MiniC
# compiler's production code. Integration boundaries still belong to C01/C09.
EXTERNAL_ROOTS = ("archiver/", "assembler/", "elf/", "linker/",
                  "preprocessor/", "third_party/")
# Original whole compiler chain, across areas. Each symbol is only an anchor
# to inspect, not a proof of its effect or ownership.
ANCHORS = {
    "C01": ("main", "minic_compile_preprocessed_file_for_target"),
    "C02": ("minic_parse_c0_program_for_target",),
    "C03": ("minic_c0_program_normalize_casts",
             "minic_c0_program_recompute_inline_emission_references"),
    "C04": ("minic_prepare_core_function_set",
             "minic_core_function_set_destroy"),
    "C05": ("minic_prune_inline_specializations_from_core",
             "minic_prune_dead_file_scope_static_objects_from_core"),
    "C06": ("minic_data_layout_validate_program",
             "minic_lp64_core_pointer_pointee"),
    "C07": ("minic_backend_emit_assembly",),
    "C08": ("minic_program_emit_write",),
    "C09": ("check-loong-machine-core-select",),
}


def git(root, *args):
    proc = subprocess.run(["git", *args], cwd=root, check=False,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode:
        raise ValueError("git " + " ".join(args) + ": " +
                         proc.stderr.decode("utf-8", "replace")[-500:])
    return proc.stdout


def source_snapshot(root):
    tracked = git(root, "ls-files", "-z").decode("utf-8", "replace").split("\0")
    tracked = [name for name in tracked if name and
               any(name.startswith(prefix) for prefix in SCAN_ROOTS)]
    return sorted(name for name in tracked if Path(name).suffix in C_SUFFIXES)


def map_slices(root, policy, state, *, check_baseline=True):
    ref = state["source_snapshot"]["observed_commit"]
    if check_baseline:
        observed = git(root, "rev-parse", "refs/remotes/origin/main").decode().strip()
        if observed != ref:
            raise ValueError("stale production baseline; S0 rebaseline required")
        # A tool-only commit can advance HEAD while the production src tree stays
        # identical. Other paths are protected by CI scope guard.
        source_head = git(root, "rev-parse", "HEAD:src").decode().strip()
        source_main = git(root, "rev-parse", "refs/remotes/origin/main:src").decode().strip()
        if source_head != source_main:
            raise ValueError("source checkout differs from pinned main")
    # Never attribute dirty working-tree code or documentation to the
    # pinned Git commit. The scan reads actual working files, not git blobs.
    protected = ("src", "tools", "include/minic",
                 "tests", "docs/architecture")
    dirty = subprocess.run(
        ["git", "diff", "--quiet", "HEAD", "--", *protected],
        cwd=root, capture_output=True, check=False
    )
    if dirty.returncode != 0:
        raise ValueError("dirty tracked source or canonical documents: refuse attribution to pinned commit")
    tracked = source_snapshot(root)
    # This second census is deliberately *repository-wide*, to detect C/H
    # files excluded by the initial narrowly scoped inventory roots.
    entire_repo = sorted(
        p for p in git(root, "ls-files", "-z").decode(
            "utf-8", "replace").split("\0")
        if p and Path(p).suffix in C_SUFFIXES
    )
    outside_scan = sorted(set(entire_repo) - set(tracked))
    explicitly_external = sorted(
        p for p in outside_scan if p.startswith(EXTERNAL_ROOTS)
    )
    unclassified = sorted(set(outside_scan) - set(explicitly_external))
    if unclassified:
        raise ValueError(
            "S0 global coverage BLOCKED: unclassified C/H source(s): " +
            ", ".join(unclassified[:15])
        )
    counts = collections.Counter()
    inventory = []
    patterns = {word: re.compile(r"\b" + re.escape(word) + r"\s*\(")
                for group in ANCHORS.values() for word in group if
                word != "check-loong-machine-core-select"}
    occurrences = collections.defaultdict(list)
    for name in tracked:
        file = root / name
        if not file.is_file():
            raise ValueError("tracked source file missing: " + name)
        lines = file.read_text(encoding="utf-8", errors="replace").splitlines()
        bucket = next((p for p in SCAN_ROOTS if name.startswith(p)), "unmapped")
        counts[bucket] += 1
        inventory.append({"path": name, "physical_lines": len(lines),
                          "sha256": hashlib.sha256(file.read_bytes()).hexdigest()})
        # Test fixture main() functions must not inflate compiler entry
        # anchors: inventory tests, but resolve production pipeline symbols
        # only in the compiler/driver source roots.
        if name.startswith("tests/"):
            continue
        for number, line in enumerate(lines, 1):
            for word, pattern in patterns.items():
                if pattern.search(line):
                    occurrences[word].append({
                        "path": name, "line": number,
                        "kind": "lexical_mention_only"
                    })
    docs = {}
    for role, name in policy["authoritative_sources"].items():
        file = root / name
        if not file.is_file():
            raise ValueError("canonical architecture source unavailable: " + name)
        docs[role] = {
            "path": name,
            "sha256": hashlib.sha256(file.read_bytes()).hexdigest(),
            "bytes": file.stat().st_size
        }
    slices = []
    unique_covered = set()
    for item in state["slices"]:
        hints = item["scope_hint"]
        matching = sorted(x for x in tracked if any(
            (x == hint if not hint.endswith(("/", "-")) else x.startswith(hint))
            for hint in hints))
        unique_covered.update(matching)
        anchors = []
        for symbol in ANCHORS.get(item["id"], ()):
            if symbol == "check-loong-machine-core-select":
                anchors.append({"symbol": symbol, "matches": [],
                                "status": "make_target_outside_C_H_scan"})
                continue
            matches = occurrences.get(symbol, [])
            anchors.append({
                "symbol": symbol, "matched_locations": len(matches),
                "examples": matches[:6],
                "status": ("LEXICAL_LOCATION_NOT_PROOF" if matches else
                           "NOT_FOUND_REQUIRES_MAPPING")
            })
        slices.append({
            "id": item["id"], "name": item["name"],
            "supervisor_status": item["status"],
            "claimed_semantic_coverage": False,
            "scope_hint": hints, "tracked_file_candidates": len(matching),
            "physical_lines_in_candidate_files": sum(
                next(v["physical_lines"] for v in inventory
                     if v["path"] == name) for name in matching
            ),
            "anchor_hints": anchors,
            "unresolved_documented_anchors": [
                a["symbol"] for a in anchors
                if a["status"] == "NOT_FOUND_REQUIRES_MAPPING"
            ],
            "evidence_needed": [
                "verified entry and producer", "true call and consumer chains",
                "state/ownership/lifetime invariants",
                "feature and target variants",
                "counterexamples and independent confirmation",
                "same-input real regression matrix"
            ]
        })
    unassigned = sorted(set(tracked) - unique_covered)
    total_lines = sum(x["physical_lines"] for x in inventory)
    return {
        "schema": "asam-breadth-inventory-v1",
        "mission_id": policy["mission_id"],
        "pinned_main_commit": ref,
        "current_head_commit": git(root, "rev-parse", "HEAD").decode().strip(),
        "source_tree": git(root, "rev-parse", "HEAD:src").decode().strip(),
        "canonical_documents": docs,
        "tracked_c_h_files": len(inventory),
        "repository_total_tracked_c_h": len(entire_repo),
        "repository_outside_scan_roots": outside_scan,
        "repository_outside_scan_roots_count": len(outside_scan),
        "repository_excluded_external_c_h": explicitly_external,
        "repository_excluded_external_c_h_count": len(explicitly_external),
        "repository_unclassified_c_h": unclassified,
        "repository_unclassified_c_h_count": len(unclassified),
        "explicit_external_roots": list(EXTERNAL_ROOTS),
        "tracked_physical_lines": total_lines,
        "file_prefix_counts": dict(sorted(counts.items())),
        "inventory_scope": list(SCAN_ROOTS),
        "slice_candidates": slices,
        "unique_candidate_files": len(unique_covered),
        "unassigned_files": unassigned,
        "unassigned_count": len(unassigned),
        "semantic_reviewed": 0,
        "unresolved_documented_anchor_count": sum(
            len(s["unresolved_documented_anchors"]) for s in slices
        ),
        "limitations": [
            "A file can participate in several functional regions.",
            "Directory membership is inventory, not proof of responsibility.",
            "A lexical symbol match is not an AST call graph or ownership proof.",
            "Canonical documents may include historical SHA; inspect current code.",
            "No semantic coverage was inferred and no source was changed."
        ],
        "source_file_fingerprints": inventory
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", default="asam-supervisor-out")
    args = parser.parse_args()
    root = args.root.resolve()
    dest = (root / args.output).resolve()
    if dest != root and root not in dest.parents:
        parser.error("output must remain inside repository checkout")
    # -I deliberately removes script directories from sys.path; import only
    # the adjacent pinned local supervisor, never arbitrary user site packages.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from supervisor import load
    policy, state = load(root)
    try:
        result = map_slices(root, policy, state)
    except (ValueError, OSError) as exc:
        parser.error(str(exc))
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "breadth-inventory.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8"
    )
    text = [
        "# ASAM: full MiniC breadth inventory (NOT audited coverage)",
        "",
        "- Mission: " + result["mission_id"],
        "- Pinned main: " + result["pinned_main_commit"],
        "- Tracked C/H files in initial scan roots: " +
            str(result["tracked_c_h_files"]),
        "- Tracked C/H files across WHOLE REPOSITORY: " +
            str(result["repository_total_tracked_c_h"]),
        "- Out-of-scope external projects / libraries (EXPLICIT): " +
            str(result["repository_excluded_external_c_h_count"]),
        "- UNCLASSIFIED C/H files (MUST BE ZERO): " +
            str(result["repository_unclassified_c_h_count"]),
        "- Physical lines: " + str(result["tracked_physical_lines"]),
        "- Unique files in planned regions: " + str(result["unique_candidate_files"]),
        "- Unassigned files: " + str(result["unassigned_count"]),
        "- Semantically reviewed: **0** (intentionally not inferred)",
        "- Documented anchors absent from current C/H lexical scan: " +
            str(result["unresolved_documented_anchor_count"]), "",
        "## Functional regions (candidate scope only)", "",
        "| Slice | Domain | Files | Lines | Lexical anchors |",
        "|---|---|---:|---:|---|"
    ]
    for s in result["slice_candidates"]:
        descriptions = ", ".join(a["symbol"] + ":" +
              str(a.get("matched_locations", 0)) for a in s["anchor_hints"])
        text.append("| " + s["id"] + " | " + s["name"] + " | " +
                    str(s["tracked_file_candidates"]) + " | " +
                    str(s["physical_lines_in_candidate_files"]) + " | " +
                    descriptions + " |")
    text += ["", "## Missing documented lexical anchors (NEEDS source review)", ""]
    for item in result["slice_candidates"]:
        if item["unresolved_documented_anchors"]:
            text.append("- " + item["id"] + ": " +
                ", ".join(item["unresolved_documented_anchors"]))
    text += ["", "## Explicitly excluded external project C/H files", ""]
    text += ["- " + x for x in result["repository_outside_scan_roots"][:100]]
    if result["repository_outside_scan_roots_count"] > 100:
        text.append("- ... additional paths in JSON")
    text += ["", "## Unassigned tracked source WITHIN scan roots", ""]
    text += ["- " + x for x in result["unassigned_files"][:80]]
    if len(result["unassigned_files"]) > 80:
        text.append("- ... additional entries available in JSON")
    text += ["", "## Caveats", ""]
    text += ["- " + x for x in result["limitations"]]
    (dest / "breadth-inventory.md").write_text(
        "\n".join(text) + "\n", encoding="utf-8"
    )
    # Nine bounded, independent read-only Mapper work orders. This is a
    # restartable multi-worker *protocol*, not nine launched LLM agents.
    packets = []
    for item in result["slice_candidates"]:
        packets.append({
            "schema": "asam-mapper-work-order-v1",
            "mission_id": result["mission_id"],
            "policy_sha256": state["policy_sha256"],
            "role": "mapper",
            "permissions": ["read_current_source", "read_canonical_docs",
                            "propose_evidence_only"],
            "prohibited": ["close_global_mission", "edit_source",
                           "claim_semantic_coverage_from_file_inventory",
                           "create_parallel_issue_register"],
            "source_revision": result["pinned_main_commit"],
            "source_tree": result["source_tree"],
            "slice_id": item["id"], "slice_name": item["name"],
            "slice_status": item["supervisor_status"],
            "candidate_file_count": item["tracked_file_candidates"],
            "source_paths_to_inspect": item["scope_hint"],
            "anchor_hints_not_semantic_proof": item["anchor_hints"],
            "canonical_doc_digests": result["canonical_documents"],
            "outputs_required": [
                "real_feature_entrypoints", "producer_and_consumers",
                "owner_and_lifetime_invariants", "cross_layer_boundary",
                "ISA_and_build_configuration_differences",
                "source_line_evidence", "counterevidence_and_uncovered_paths",
                "canonical_issue_register_matches"
            ],
            "status_after_generation": "NOT_MAPPED",
            "requires_independent_review": True
        })
    bundle = {
        "schema": "asam-breadth-work-orders-v1",
        "mission_id": result["mission_id"],
        "source_revision": result["pinned_main_commit"],
        "global_goal_completed": False,
        "state_epoch": state["epoch"],
        "work_orders": packets,
        "note": "Independent task specifications only; no new AI agents run."
    }
    (dest / "work-orders.json").write_text(
        json.dumps(bundle, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8"
    )
    print("ASAM_BREADTH_FILES=" + str(result["tracked_c_h_files"]))
    print("ASAM_BREADTH_LINES=" + str(result["tracked_physical_lines"]))
    print("ASAM_BREADTH_SLICES=" + str(len(result["slice_candidates"])))
    print("ASAM_UNASSIGNED_FILES=" + str(result["unassigned_count"]))
    print("ASAM_REPO_TOTAL_C_H=" +
          str(result["repository_total_tracked_c_h"]))
    print("ASAM_REPO_OUTSIDE_SCAN_ROOTS=" +
          str(result["repository_outside_scan_roots_count"]))
    print("ASAM_REPO_EXCLUDED_EXTERNAL_C_H=" +
          str(result["repository_excluded_external_c_h_count"]))
    print("ASAM_REPO_UNCLASSIFIED_C_H=" +
          str(result["repository_unclassified_c_h_count"]))
    for name in result["repository_outside_scan_roots"][:40]:
        print("ASAM_UNSCANNED=" + name)
    print("ASAM_STALE_ANCHOR_HINTS=" +
          str(result["unresolved_documented_anchor_count"]))
    print("ASAM_SEMANTIC_COVERAGE=0_UNVERIFIED")


if __name__ == "__main__":
    sys.exit(main())
