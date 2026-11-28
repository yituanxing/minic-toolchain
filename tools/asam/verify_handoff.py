#!/usr/bin/env python3
"""Independent deterministic attestation of a chat-free Mapper handoff.

Intentionally does NOT import supervisor.py or topology.py. A new controller
must check the three exported artifacts against the pinned live repository,
not blindly trust a generated prompt or older AI conversation.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys


class InvalidHandoff(ValueError):
    pass


def require(cond, reason):
    if not cond:
        raise InvalidHandoff(reason)


def sh(root, *args):
    proc = subprocess.run(["git", *args], cwd=root, capture_output=True,
                          text=True, check=False)
    require(proc.returncode == 0, "git provenance unavailable")
    return proc.stdout.strip()


def check(root, policy, state, resume, inventory, orders):
    pin = hashlib.sha256((root / "tools/asam/control/policy.json").read_bytes()).hexdigest()
    require(pin == state["policy_sha256"], "mission policy content mismatch")
    require(resume["policy_sha256"] == pin, "restart packet policy mismatch")
    require(resume["policy_id"] == policy["mission_id"] ==
            inventory["mission_id"] == orders["mission_id"] ==
            state["policy_id"], "mission substitution")
    require(resume["global_mission"] == policy["purpose"], "mission rewritten")
    require(resume["global_task_completed"] is False and
            orders["global_goal_completed"] is False and
            state["mission_complete"] is False, "premature global closure")
    require(resume["nonnegotiable"] == policy["nonnegotiable"],
            "permanent invariant omitted")
    require(resume["canonical_sources"] == policy["authoritative_sources"],
            "canonical document authority redirected")
    require(resume["work_unit"] == state["work_limit"],
            "complete functional work unit replaced")
    require(resume["project"] == state["project"], "project boundary drift")
    require(not resume["temporary_context"], "temporary model context smuggled in")
    require(resume["source_baseline"] == state["source_snapshot"],
            "resume snapshot differs from canonical scheduler state")
    commit = state["source_snapshot"]["observed_commit"]
    require(commit == inventory["pinned_main_commit"] ==
            orders["source_revision"] == sh(root, "rev-parse", "origin/main"),
            "stale or swapped source revision")
    source_tree = sh(root, "rev-parse", "HEAD:src")
    require(inventory["source_tree"] == source_tree,
            "different actual compiler source tree")
    require(orders["state_epoch"] == state["epoch"], "stale task work-order epoch")
    refs = policy["authoritative_sources"]
    require(set(inventory["canonical_documents"]) == set(refs),
            "missing authoritative registry")
    for key, relative in refs.items():
        record = inventory["canonical_documents"][key]
        path = root / relative
        require(record["path"] == relative and
                record["sha256"] == hashlib.sha256(path.read_bytes()).hexdigest() and
                record["bytes"] == path.stat().st_size,
                "authoritative source tampered: " + key)

    slices = {s["id"]: s for s in state["slices"]}
    require(len(slices) == len(state["slices"]) == 10, "duplicate or lost functional region")
    expected_counts = {}
    for s in state["slices"]:
        expected_counts[s["status"]] = expected_counts.get(s["status"], 0) + 1
    for status, amount in expected_counts.items():
        require(resume["coverage_status"].get(status) == amount,
                "status count differs from actual scheduler")
    require(sum(resume["coverage_status"].values()) == len(slices),
            "missing or inflated coverage count")
    require(all(not s["verified"] for s in state["slices"]),
            "unauthenticated semantic verification accepted")
    require(inventory["semantic_reviewed"] == 0,
            "lexical inventory falsely promoted to semantic review")
    require(inventory["unassigned_count"] == 0,
            "unassigned tracked source has no next mapping domain")
    records = {s["id"]: s for s in inventory["slice_candidates"]}
    require(set(records) == set(slices), "inventory silently dropped a region")
    plan = [s for s in state["slices"] if s["status"] == "PLANNED"]
    front = plan if plan else [s for s in state["slices"] if s["status"] != "VERIFIED"]
    require(resume["breadth_frontier"] == [
        {"id": s["id"], "name": s["name"], "status": s["status"],
         "scope_hint": s["scope_hint"]} for s in front
    ], "restart frontier re-ordered or narrowed")
    if front:
        require(resume["selected"]["id"] == front[0]["id"] and
                resume["selected"]["scope_hint"] == front[0]["scope_hint"],
                "new agent steered away from breadth-first next task")

    work = orders["work_orders"]
    require(len(work) == len(slices) and
            len({w["slice_id"] for w in work}) == len(slices),
            "missing or duplicated independent Mapper task")
    for one in work:
        ident = one["slice_id"]
        require(ident in slices, "unknown Mapper region")
        s, record = slices[ident], records[ident]
        require(one["schema"] == "asam-mapper-work-order-v1" and
                one["mission_id"] == policy["mission_id"] and
                one["policy_sha256"] == pin and
                one["role"] == "mapper", "wrong role or altered authority")
        require(one["permissions"] == [
            "read_current_source", "read_canonical_docs",
            "propose_evidence_only"
        ], "worker received unauthorized permissions")
        require(one["prohibited"] == [
            "close_global_mission", "edit_source",
            "claim_semantic_coverage_from_file_inventory",
            "create_parallel_issue_register"
        ], "worker has weakened prohibitions")
        require(one["source_revision"] == commit and
                one["source_tree"] == source_tree and
                one["slice_status"] == s["status"] and
                one["source_paths_to_inspect"] == s["scope_hint"] and
                one["slice_name"] == s["name"], "worker was sent stale or wrong domain")
        require(one["candidate_file_count"] == record["tracked_file_candidates"] and
                one["anchor_hints_not_semantic_proof"] == record["anchor_hints"],
                "discovery evidence not matching inventory")
        require(one["canonical_doc_digests"] == inventory["canonical_documents"],
                "a worker was given outdated authority fingerprints")
        require(one["status_after_generation"] == "NOT_MAPPED" and
                one["requires_independent_review"] is True,
                "a Mapper proposed its own semantic certification")
    return len(work)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--resume", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--orders", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    def read(path):
        return json.loads(path.read_text(encoding="utf-8"))
    try:
        policy = read(root / "tools/asam/control/policy.json")
        state = read(root / "tools/asam/control/state.json")
        resume, inventory, orders = (read(args.resume),
                                     read(args.inventory), read(args.orders))
        count = check(root, policy, state, resume, inventory, orders)
    except (InvalidHandoff, OSError, KeyError, TypeError, ValueError) as exc:
        print("ASAM_HANDOFF=REJECT " + str(exc), file=sys.stderr)
        return 2
    print("ASAM_HANDOFF=PASS fresh controller packet and " +
          str(count) + " permission-bounded read-only Mapper work orders")
    return 0


if __name__ == "__main__":
    sys.exit(main())
