#!/usr/bin/env python3
"""Negative and positive tests for the ASAM fail-closed handoff gate."""
import copy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import handoff_gate as gate

contract = {
    "schema": "asam-v1-task-contract-1",
    "mode": "READ_ONLY_TRIAGE",
    "forbidden_actions": ["production_source_edits"],
    "max_model_packets": 3,
}
member = {
    "content_sha256": "a" * 64,
    "lexical_reference_sha256": "b" * 64,
    "truncated": False,
}
packet = {
    "schema": "asam-v1-scout-packet-1",
    "source_revision": "d" * 40,
    "packets": [{"id": "ASAM-one", "a": member, "b": member,
                 "review_status": "NEEDS_SEMANTIC_EVIDENCE"}],
}
empty_model = {
    "schema": "asam-v1-scout-result-1",
    "status": "DRY_RUN", "source_revision": "d" * 40,
    "reviewed_count": 0, "results": [],
}
dry = gate.enforce(contract, packet, empty_model)
assert dry["status"] == "NO_MODEL_CONNECTED"
assert dry["fixer_tasks_authorized"] == 0

model = {
    "schema": "asam-v1-scout-result-1",
    "status": "TRIAGED_NO_EDITS", "source_revision": "d" * 40,
    "reviewed_count": 1,
    "results": [{
        "id": "ASAM-one", "reviewed_revision": "d" * 40,
        "approval_to_edit": False, "token_usage": {"total_tokens": 200},
        "triage": {
            "classification": "POSSIBLE_OVERLAP",
            "reason": "matching implementation shape, contracts unverified",
            "evidence_gaps": ["caller invariants", "build variants"],
            "next_checks": ["inspect callers"],
            "risk": "high"
        }
    }]
}
triaged = gate.enforce(contract, packet, model)
assert triaged["status"] == "TRIAGE_ONLY"
assert triaged["queue"][0]["state"] == "REQUIRES_FURTHER_EVIDENCE"
assert triaged["fixer_tasks_authorized"] == 0

def blocked(reason, changed_contract=contract, changed_packet=packet,
            changed_model=model):
    try:
        gate.enforce(changed_contract, changed_packet, changed_model)
    except gate.GateError:
        return
    raise AssertionError("unsafe case accepted: " + reason)

tampered = copy.deepcopy(model)
tampered["results"][0]["approval_to_edit"] = True
blocked("AI self-approval", changed_model=tampered)
tampered = copy.deepcopy(model)
tampered["results"][0]["patch"] = "remove all functions"
blocked("unexpected patch output", changed_model=tampered)
tampered = copy.deepcopy(model)
tampered["results"][0]["id"] = "ASAM-other"
blocked("unknown task", changed_model=tampered)
tampered = copy.deepcopy(model)
tampered["results"][0]["reviewed_revision"] = "e" * 40
blocked("stale source", changed_model=tampered)
tampered = copy.deepcopy(contract)
tampered["mode"] = "AUTOMATIC_FIX"
blocked("contract drift", changed_contract=tampered)
tampered = copy.deepcopy(contract)
tampered["forbidden_actions"] = []
blocked("weakened contract", changed_contract=tampered)
tampered = copy.deepcopy(packet)
tampered["packets"][0]["a"] = {"content_sha256": "a" * 64}
blocked("missing reference evidence", changed_packet=tampered)
tampered = copy.deepcopy(model)
tampered["results"][0]["triage"]["classification"] = "DELETE_CODE_NOW"
blocked("non-schema verdict", changed_model=tampered)
tampered = copy.deepcopy(model)
tampered["status"] = "MODEL_ERROR"
blocked("model error should not pass", changed_model=tampered)

print("ASAM gate selftest PASS: 2 valid states and 9 fail-closed cases")
