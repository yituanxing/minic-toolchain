#!/usr/bin/env python3
"""Negative and positive tests for the ASAM fail-closed handoff gate."""
import copy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import handoff_gate as gate
import scout_api as api
from unittest.mock import patch

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
controller = copy.deepcopy(model)
controller["status"] = "CONTROLLER_TRIAGE_NO_EDITS"
controller["reviewer_type"] = "CHATGPT_CONTROLLER"
controller_result = gate.enforce(contract, packet, controller)
assert controller_result["status"] == "CONTROLLER_TRIAGE_ONLY"
assert controller_result["fixer_tasks_authorized"] == 0

def blocked(reason, changed_contract=contract, changed_packet=packet,
            changed_model=model):
    try:
        gate.enforce(changed_contract, changed_packet, changed_model)
    except gate.GateError:
        return
    raise AssertionError("unsafe case accepted: " + reason)

tampered = copy.deepcopy(controller)
del tampered["reviewer_type"]
blocked("controller without provenance", changed_model=tampered)
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

# Mock only the network boundary. This does NOT constitute a real model call.
class FakeHTTP:
    def __enter__(self):
        return self
    def __exit__(self, exc_type, exc_val, traceback):
        return False
    def read(self, size=-1):
        return b""

verdict = model["results"][0]["triage"]
mock_result = {
    "status": "completed",
    "output": [{"type": "message", "content": [{
        "type": "output_text", "text": __import__("json").dumps(verdict)
    }]}],
    "usage": {"total_tokens": 157}
}
class FakeResponse(FakeHTTP):
    def __iter__(self):
        return iter([])
    def read(self, size=-1):
        return __import__("json").dumps(mock_result).encode()

def fake_urlopen(request, timeout=0):
    payload = __import__("json").loads(request.data.decode("utf-8"))
    assert payload["store"] is False
    assert payload["model"] == "mock-review-model"
    assert payload["max_output_tokens"] <= 800
    assert "READ_ONLY" not in request.full_url or request.full_url.startswith("https://")
    assert request.full_url == "https://api.openai.com/v1/responses"
    assert payload["text"]["format"]["type"] == "json_schema"
    return FakeResponse()

with patch.object(api.urllib.request, "urlopen", fake_urlopen):
    reviewed, usage = api.run_one(packet["packets"][0], "mock-key", "mock-review-model")
    assert reviewed == verdict
    assert usage["total_tokens"] == 157

# Strict schema: an AI that smuggles patch metadata must be rejected.
corrupt = dict(verdict)
corrupt["patch"] = "remove all code"
mock_result["output"][0]["content"][0]["text"] = __import__("json").dumps(corrupt)
with patch.object(api.urllib.request, "urlopen", fake_urlopen):
    try:
        api.run_one(packet["packets"][0], "mock-key", "mock-review-model")
    except ValueError:
        pass
    else:
        raise AssertionError("model adapter accepted a patch field")

print("ASAM gate selftest PASS: safety rejections and mocked API protocol")

