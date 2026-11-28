#!/usr/bin/env python3
"""Deterministic, fail-closed handoff gate between Scout and any future Fixer.

The model can ONLY create investigation recommendations. No model text is an
authorization to modify production source, remove tests or approve a merge.
"""
import argparse
import json
from pathlib import Path


class GateError(ValueError):
    pass


CLASSES = {"DISTINCT_KEEP", "POSSIBLE_OVERLAP", "INSUFFICIENT_EVIDENCE"}
ALLOWED_MODEL_FIELDS = {"classification", "reason", "evidence_gaps",
                        "next_checks", "risk"}
ALLOWED_RESULT_FIELDS = {"id", "triage", "token_usage",
                         "reviewed_revision", "approval_to_edit"}


def enforce(contract, packets, model):
    if contract.get("schema") != "asam-v1-task-contract-1":
        raise GateError("unrecognized task contract")
    if contract.get("mode") != "READ_ONLY_TRIAGE":
        raise GateError("task mode must be READ_ONLY_TRIAGE")
    if "production_source_edits" not in contract.get("forbidden_actions", []):
        raise GateError("contract must prohibit source edits")
    if packets.get("schema") != "asam-v1-scout-packet-1":
        raise GateError("unexpected review packet schema")
    if model.get("schema") != "asam-v1-scout-result-1":
        raise GateError("unexpected model report schema")
    if model.get("source_revision") != packets.get("source_revision"):
        raise GateError("model reviewed stale revision")
    if packets.get("source_revision") in ("", "unknown", None):
        raise GateError("missing fixed source revision")
    if not 1 <= int(contract.get("max_model_packets", 0)) <= 3:
        raise GateError("model review budget outside safe bounds")
    available = {item["id"]: item for item in packets.get("packets", [])}
    if len(available) != len(packets.get("packets", [])):
        raise GateError("duplicate Scout IDs")
    for item in available.values():
        if item.get("review_status") != "NEEDS_SEMANTIC_EVIDENCE":
            raise GateError("Scout packet has invalid review status")
        for side in ("a", "b"):
            member = item[side]
            if member.get("truncated") and not member.get("shown_through"):
                raise GateError("truncated body without evidence bounds")
            if not member.get("content_sha256") or not member.get(
                "lexical_reference_sha256"
            ):
                raise GateError("missing function or reference fingerprint")
    status = model.get("status")
    if status not in {"DRY_RUN", "NOT_CONFIGURED", "TRIAGED_NO_EDITS",
                      "CONTROLLER_TRIAGE_NO_EDITS"}:
        raise GateError("model report not complete and safe")
    results = model.get("results", [])
    if not isinstance(results, list) or len(results) > contract["max_model_packets"]:
        raise GateError("model exceeded task budget")
    if model.get("reviewed_count") != len(results):
        raise GateError("model count does not match results")
    if status not in ("TRIAGED_NO_EDITS", "CONTROLLER_TRIAGE_NO_EDITS") and results:
        raise GateError("non-triage status contains decisions")
    if status == "CONTROLLER_TRIAGE_NO_EDITS" and (
        model.get("reviewer_type") != "CHATGPT_CONTROLLER"
    ):
        raise GateError("controller verdict missing explicit provenance")
    seen = set()
    queue = []
    for result in results:
        if set(result) - ALLOWED_RESULT_FIELDS:
            raise GateError("model result contains forbidden or unknown fields")
        ident = result.get("id")
        if ident in seen or ident not in available:
            raise GateError("unknown or duplicate task ID")
        seen.add(ident)
        if result.get("approval_to_edit") is not False:
            raise GateError("model is not allowed to authorize changes")
        if result.get("reviewed_revision") != packets["source_revision"]:
            raise GateError("one model verdict was produced on stale source")
        triage = result.get("triage")
        if not isinstance(triage, dict) or set(triage) != ALLOWED_MODEL_FIELDS:
            raise GateError("incomplete or unexpected semantic triage fields")
        if triage["classification"] not in CLASSES or triage["risk"] not in (
            "low", "medium", "high"
        ):
            raise GateError("invalid model classification or risk")
        if not all(isinstance(triage[key], list) for key in (
            "evidence_gaps", "next_checks"
        )):
            raise GateError("invalid evidence list")
        if triage["classification"] == "DISTINCT_KEEP":
            disposition = "PENDING_INDEPENDENT_CONFIRMATION"
        else:
            disposition = "REQUIRES_FURTHER_EVIDENCE"
        queue.append({
            "id": ident, "source_revision": packets["source_revision"],
            "state": disposition, "classification": triage["classification"],
            "risk": triage["risk"], "independent_verified": False,
            "source_change_authorized": False, "fixer_job_created": False,
            "missing_evidence": triage["evidence_gaps"],
            "next_checks": triage["next_checks"],
        })
    return {
        "schema": "asam-v1-handoff-gate-1",
        "status": ("NO_MODEL_CONNECTED" if status in ("DRY_RUN", "NOT_CONFIGURED")
                   else "CONTROLLER_TRIAGE_ONLY" if status == "CONTROLLER_TRIAGE_NO_EDITS"
                   else "TRIAGE_ONLY"),
        "source_revision": packets["source_revision"],
        "contract_mode": contract["mode"],
        "candidate_count": len(available),
        "reviewed_count": len(queue),
        "fixer_tasks_authorized": 0,
        "independent_verifier_required": True,
        "queue": queue,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", default=".")
    parser.add_argument("--packets", default="asam-out/scout-packets.json")
    parser.add_argument("--model", default="asam-out/scout-model-status.json")
    parser.add_argument("--contract", default="tools/asam/task_contract.json")
    parser.add_argument("--output", default="asam-out")
    args = parser.parse_args()
    root = Path(args.repo).resolve()

    def load(rel):
        path = (root / rel).resolve()
        if not (path == root or root in path.parents):
            parser.error("input path outside repository")
        return json.loads(path.read_text(encoding="utf-8"))

    contract, packets, model = (
        load(args.contract), load(args.packets), load(args.model)
    )
    try:
        result = enforce(contract, packets, model)
    except (GateError, KeyError, TypeError) as error:
        parser.error("handoff refused: " + str(error))
    dest = (root / args.output).resolve()
    if not (dest == root or root in dest.parents):
        parser.error("output path outside repository")
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "handoff-gate.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print("ASAM gate:", result["status"],
          "reviewed:", result["reviewed_count"],
          "authorized Fixer tasks:", result["fixer_tasks_authorized"])


if __name__ == "__main__":
    main()
