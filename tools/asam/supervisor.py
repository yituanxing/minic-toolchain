#!/usr/bin/env python3
"""ASAM deterministic Supervisor prototype.

The Supervisor does not reason over source and is not an AI agent.
It refuses goal drift and restores the whole-project task from durable files.
Any 'verified' status requires independent evidence pointers, not model claims.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

PINNED_MISSION_ID = "ASAM-MINIC-GLOBAL-20261128"
PINNED_POLICY_SHA256 = "951d0e9cfb2b8e67b3f40426a02e441c6704f0c83938df15ef3662c23f95fd56"
EXPECTED_SOURCE = "yituanxing/minic-toolchain"
STATUSES = ("PLANNED", "MAPPED", "INVESTIGATING", "CONFIRMED",
            "FIXED_UNVERIFIED", "VERIFIED")
ALLOWED = {
    "PLANNED": ("MAPPED",),
    "MAPPED": ("INVESTIGATING", "VERIFIED"),
    "INVESTIGATING": ("CONFIRMED", "VERIFIED"),
    "CONFIRMED": ("FIXED_UNVERIFIED",),
    "FIXED_UNVERIFIED": ("VERIFIED",),
    "VERIFIED": (),
}
ROLE_FOR = {
    "MAPPED": "mapper", "INVESTIGATING": "scout",
    "CONFIRMED": "challenger", "FIXED_UNVERIFIED": "fixer",
    "VERIFIED": "verifier",
}
SHA = re.compile(r"^[0-9a-f]{40}$")


class Rejected(ValueError):
    """A rejected transition or invalid persistent state."""


def must(condition, message):
    if not condition:
        raise Rejected(message)


def tree(root):
    return root / "tools" / "asam" / "control"


def paths(root):
    return tree(root) / "policy.json", tree(root) / "state.json"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(root, *, require_canonical=True):
    policy_path, state_path = paths(root)
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    state = json.loads(state_path.read_text(encoding="utf-8"))
    must(policy.get("schema") == "asam-governance-v1", "wrong mission schema")
    must(policy.get("mission_id") == PINNED_MISSION_ID, "mission identity drift")
    digest = sha256(policy_path)
    must(digest == PINNED_POLICY_SHA256, "original mission has changed")
    must(state.get("policy_id") == PINNED_MISSION_ID, "queue switched mission")
    must(state.get("policy_sha256") == digest, "queue has wrong mission digest")
    must(state.get("schema") == "asam-control-state-v1", "wrong queue schema")
    must(state.get("project") == EXPECTED_SOURCE, "project scope changed")
    must(state.get("global_status") == "ACTIVE" and
         state.get("mission_complete") is False,
         "worker cannot close or replace global mission")
    must(state.get("schedule_mode") == "BREADTH_FIRST",
         "scheduler has deviated from breadth-first mission")
    must(isinstance(state.get("epoch"), int) and state["epoch"] >= 0,
         "invalid state epoch")
    must(SHA.fullmatch(state.get("source_snapshot", {}).get(
         "observed_commit", "")) is not None, "invalid source baseline")
    must(policy.get("nonnegotiable") and
         len(policy["nonnegotiable"]) >= 16, "missing permanent constraints")
    refs = policy.get("authoritative_sources", {})
    must(len(refs) >= 5 and all(
         v.startswith("docs/architecture/") and v.endswith(".md")
         for v in refs.values()), "canonical documents missing from mission")
    if require_canonical:
        for path in refs.values():
            must((root / path).is_file(), "missing canonical source: " + path)
    ids = set()
    slices = state.get("slices")
    must(isinstance(slices, list) and len(slices) >= 9,
         "breadth map was truncated")
    for item in slices:
        ident = item.get("id")
        must(isinstance(ident, str) and re.fullmatch(r"C\d{2}", ident)
             and ident not in ids, "duplicate or missing functional slice")
        ids.add(ident)
        status = item.get("status")
        must(status in STATUSES, "illegal slice state for " + ident)
        must(bool(item.get("scope_hint")), "scope lost: " + ident)
        must(isinstance(item.get("canonical_evidence_refs"), list),
             "missing source references: " + ident)
        if status == "VERIFIED":
            must(item.get("verified") is True,
                 "verified label lacks verified flag")
            must(SHA.fullmatch(item.get("last_verified_revision") or ""),
                 "verified slice lacks exact source revision")
            proof = item.get("verification", {})
            must(isinstance(proof, dict) and proof.get("independent") is True,
                 "verified slice lacks independent verification")
            must(item["canonical_evidence_refs"],
                 "verified slice lacks canonical source record")
        else:
            must(item.get("verified") is False and
                 item.get("last_verified_revision") is None,
                 "non-verified slice falsely claims verification")
    active = state.get("active_task")
    must(active is None or (isinstance(active, dict)
         and active.get("slice_id") in ids), "active task points outside graph")
    return policy, state


def progress(state):
    counts = {name: 0 for name in STATUSES}
    for item in state["slices"]:
        counts[item["status"]] += 1
    return counts


def handoff(policy, state):
    slices = state["slices"]
    remaining = [x for x in slices if x["status"] != "VERIFIED"]
    planned = [x for x in remaining if x["status"] == "PLANNED"]
    # Map all domains before deep-diving into the same one forever.
    breadth = planned if planned else remaining
    selected = next((x for x in slices if state.get("active_task")
                     and x["id"] == state["active_task"]["slice_id"]),
                    None)
    if selected is None:
        selected = breadth[0] if breadth else None
    return {
        "schema": "asam-resume-packet-v1",
        "policy_id": policy["mission_id"],
        "policy_sha256": state["policy_sha256"],
        "global_mission": policy["purpose"],
        "global_status": state["global_status"],
        "global_task_completed": False,
        "project": state["project"],
        "source_baseline": state["source_snapshot"],
        "nonnegotiable": policy["nonnegotiable"],
        "canonical_sources": policy["authoritative_sources"],
        "audit_stages": policy["audit_stages"],
        "work_unit": state["work_limit"],
        "coverage_status": progress(state),
        "breadth_frontier": [
            {"id": x["id"], "name": x["name"], "status": x["status"],
             "scope_hint": x["scope_hint"]}
            for x in breadth
        ],
        "selected": ({
            "id": selected["id"], "name": selected["name"],
            "current_stage": selected["audit_stage"],
            "status": selected["status"], "scope_hint": selected["scope_hint"],
            "canonical_evidence_refs": selected["canonical_evidence_refs"],
            "instruction": ("Read existing canonical docs and real code; "
                            "map entrance/producer/consumers/ownership, "
                            "record uncovered paths. Do NOT substitute a "
                            "single-file clean-up for global coverage.")
        } if selected else None),
        "recent_history_pointers_only": state["recorded_events"][-8:],
        "temporary_context": [],
        "reset_contract": "This packet is sufficient to restart a new AI task. "
                          "Do not rely on earlier model messages. "
                          "The model cannot mark the global mission done."
    }


def transition(policy, state, proposal):
    required = {"schema", "mission_id", "expected_epoch", "slice_id",
                "to_status", "actor_role", "actor_id", "evidence"}
    must(isinstance(proposal, dict) and set(proposal) == required,
         "transition contains unexpected or missing fields")
    must(proposal["schema"] == "asam-transition-v1" and
         proposal["mission_id"] == policy["mission_id"],
         "transition requests a different mission")
    must(proposal["expected_epoch"] == state["epoch"],
         "stale concurrent state write")
    ident = proposal["slice_id"]
    item = next((s for s in state["slices"] if s["id"] == ident), None)
    must(item is not None, "unknown functional subgraph")
    target = proposal["to_status"]
    must(target in ALLOWED[item["status"]], "illegal lifecycle transition")
    # Evidence URLs and reviewer names can be forged by untrusted workers.
    # This prototype deliberately REFUSES terminal VERIFIED until an external
    # trusted verifier checks CI provenance, source SHA and project-owner grant.
    must(target != "VERIFIED",
         "VERIFIED disabled until externally authenticated evidence gate exists")
    must(proposal["actor_role"] == ROLE_FOR[target], "wrong worker role")
    must(isinstance(proposal["actor_id"], str) and len(
         proposal["actor_id"].strip()) >= 3, "worker identity missing")
    ev = proposal["evidence"]
    must(isinstance(ev, dict), "evidence object required")
    must(ev.get("source_revision") == state["source_snapshot"]["observed_commit"],
         "proposal built on wrong code baseline")
    must(ev.get("canonical_ref") in policy["authoritative_sources"].values(),
         "evidence must point into existing authoritative registry")
    must(isinstance(ev.get("link"), str) and (
         ev["link"].startswith("https://github.com/") or
         ev["link"].startswith("docs/architecture/")),
         "traceable evidence pointer required")
    if target == "MAPPED":
        must(bool(ev.get("producer")) and bool(ev.get("consumers")) and
             bool(ev.get("functional_path")), "functional map incomplete")
    elif target == "CONFIRMED":
        must(ev.get("counterevidence_checked") is True and
             proposal["actor_id"] != ev.get("scout_actor"),
             "confirmation requires independent challenge")
    elif target == "FIXED_UNVERIFIED":
        must(bool(ev.get("fix_commit")) and SHA.fullmatch(ev["fix_commit"]),
             "fixer must provide exact changed commit")
    elif target == "VERIFIED":
        must(ev.get("independent") is True and
             ev.get("verifier_actor") == proposal["actor_id"] and
             ev.get("verifier_actor") != ev.get("fixer_actor"),
             "fixer cannot approve own changes")
        must(bool(ev.get("matrix")) and bool(ev.get("real_input_oracle")) and
             bool(ev.get("negative_evidence")) and bool(ev.get("coverage_complete")),
             "not enough independent same-input verification")
        must(SHA.fullmatch(ev.get("tested_commit", "")) is not None,
             "verification lacks exact tested revision")
    changed = copy.deepcopy(state)
    target_item = next(x for x in changed["slices"] if x["id"] == ident)
    target_item["status"] = target
    target_item["audit_stage"] = {
        "MAPPED": "S1_FUNCTIONAL_MAP",
        "INVESTIGATING": "S2_BREADTH_DISCOVERY",
        "CONFIRMED": "S3_INDEPENDENT_COUNTEREVIDENCE",
        "FIXED_UNVERIFIED": "S4_ROOT_CAUSE_FIX",
        "VERIFIED": "S5_SAME_INPUT_VERIFICATION",
    }[target]
    target_item["canonical_evidence_refs"].append(ev["link"])
    if target == "VERIFIED":
        target_item["verified"] = True
        target_item["last_verified_revision"] = ev["tested_commit"]
        target_item["verification"] = {
            "independent": True, "verifier_actor": ev["verifier_actor"],
            "fixer_actor": ev.get("fixer_actor"), "matrix": ev["matrix"],
            "real_input_oracle": ev["real_input_oracle"]
        }
    changed["epoch"] += 1
    changed["recorded_events"].append({
        "kind": "SLICE_TRANSITION", "id": ident,
        "from": item["status"], "to": target,
        "actor_id": proposal["actor_id"], "source": ev["link"],
        "epoch": changed["epoch"]
    })
    return changed


def write_atomic(path, state):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent,
        prefix=".asam-state-", delete=False
    ) as stream:
        temp = Path(stream.name)
        json.dump(state, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    try:
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("check")
    sub.add_parser("resume")
    sub.add_parser("status")
    ap = sub.add_parser("apply")
    ap.add_argument("--proposal", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        policy, state = load(root)
        if args.command == "check":
            print("ASAM_MISSION_GUARD=PASS mission=" + policy["mission_id"])
            print("ASAM_BREADTH_FRONTIER=" +
                  str(sum(s["status"] != "VERIFIED" for s in state["slices"])))
            return 0
        if args.command == "status":
            print(json.dumps({
                "policy_id": policy["mission_id"],
                "global_status": state["global_status"],
                "complete": False, "epoch": state["epoch"],
                "coverage": progress(state)
            }, indent=2))
            return 0
        if args.command == "resume":
            print(json.dumps(handoff(policy, state), indent=2, ensure_ascii=False))
            return 0
        proposed = json.loads(args.proposal.read_text(encoding="utf-8"))
        updated = transition(policy, state, proposed)
        write_atomic(paths(root)[1], updated)
        print("ASAM_TRANSITION=PASS epoch=" + str(updated["epoch"]) +
              " (global mission remains ACTIVE)")
        return 0
    except (Rejected, KeyError, TypeError, ValueError, OSError) as exc:
        print("ASAM_MISSION_GUARD=REJECT " + str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
