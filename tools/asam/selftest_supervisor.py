#!/usr/bin/env python3
"""Verify 'forget the conversation, not the project' with fresh processes.

All test state changes occur in a throwaway directory. No production C file,
authoritative MiniC issue register, or GitHub branch is modified by selftests.
"""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import supervisor as sup


def refuse(fn, message):
    try:
        fn()
    except sup.Rejected:
        return
    raise AssertionError("Supervisor accepted invalid state: " + message)


def fresh_process(script, root, action):
    return subprocess.run(
        [sys.executable, "-I", str(script), "--root", str(root), action],
        capture_output=True, text=True, cwd=root, check=False
    )


repo = Path(__file__).resolve().parents[2]
policy, original = sup.load(repo)
script = Path(sup.__file__).resolve()
assert original["mission_complete"] is False
assert len(original["slices"]) == len(sup.EXPECTED_HINTS) == 10
assert all(not x["verified"] for x in original["slices"])

with tempfile.TemporaryDirectory(prefix="asam-reset-") as td:
    root = Path(td)
    control = sup.tree(root)
    control.mkdir(parents=True)
    shutil.copy2(sup.paths(repo)[0], sup.paths(root)[0])
    shutil.copy2(sup.paths(repo)[1], sup.paths(root)[1])
    for ref in policy["authoritative_sources"].values():
        # Test only the existence/pointer contract: actual facts remain in
        # their real, canonical documents in the production repository.
        path = root / ref
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("Fixture for authoritative reference path\n",
                        encoding="utf-8")
    init = fresh_process(script, root, "resume")
    assert init.returncode == 0, init.stderr
    first = json.loads(init.stdout)
    assert first["global_task_completed"] is False
    assert first["policy_id"] == sup.PINNED_MISSION_ID
    assert len(first["breadth_frontier"]) == 10
    assert first["selected"]["id"] == "C01"
    assert first["temporary_context"] == []
    assert len(first["nonnegotiable"]) >= 16
    assert "minic-global-contract-map.md" in repr(first["canonical_sources"])

    proposal = {
        "schema": "asam-transition-v1",
        "mission_id": policy["mission_id"],
        "expected_epoch": original["epoch"],
        "slice_id": "C01",
        "to_status": "MAPPED",
        "actor_role": "mapper",
        "actor_id": "independent-mapper-01",
        "evidence": {
            "source_revision": original["source_snapshot"]["observed_commit"],
            "canonical_ref": policy["authoritative_sources"]["architecture"],
            "link": "https://github.com/yituanxing/minic-toolchain/blob/"
                    + original["source_snapshot"]["observed_commit"]
                    + "/docs/architecture/minic-global-contract-map.md",
            "producer": "driver invocation object",
            "consumers": ["frontend parser", "compiler orchestration"],
            "functional_path": "CLI invocation -> parser -> compiler"
        }
    }
    proposal_path = root / "proposal.json"
    proposal_path.write_text(json.dumps(proposal), encoding="utf-8")
    apply = subprocess.run(
        [sys.executable, "-I", str(script), "--root", str(root),
         "apply", "--proposal", str(proposal_path)],
        capture_output=True, text=True, cwd=root
    )
    assert apply.returncode == 0, apply.stderr
    after = fresh_process(script, root, "resume")
    assert after.returncode == 0, after.stderr
    restored = json.loads(after.stdout)
    assert restored["coverage_status"]["MAPPED"] == 1
    assert restored["coverage_status"]["PLANNED"] == 9
    assert restored["selected"]["id"] == "C02"
    assert restored["global_task_completed"] is False
    assert restored["policy_sha256"] == first["policy_sha256"]
    assert restored["global_mission"] == first["global_mission"]
    assert restored["temporary_context"] == []
    assert restored["source_baseline"] == first["source_baseline"]

    state = json.loads(sup.paths(root)[1].read_text(encoding="utf-8"))
    refuse(lambda: sup.transition(policy, state, proposal),
           "replay of earlier epoch")

    def bad_proposal(**overrides):
        d = copy.deepcopy(proposal)
        d["expected_epoch"] = state["epoch"]
        d.update(overrides)
        return d

    refuse(lambda: sup.transition(policy, state, bad_proposal(
        to_status="VERIFIED")), "unreviewed slice marked verified")
    refuse(lambda: sup.transition(policy, state, bad_proposal(
        mission_id="ASAM-SMALL-LOCAL-CLEANUP")), "mission replacement")
    refuse(lambda: sup.transition(policy, state, bad_proposal(
        to_status="INVESTIGATING", actor_role="fixer")), "wrong role")
    refuse(lambda: sup.transition(policy, state, bad_proposal(
        slice_id="C99")), "unknown function graph node")
    refused_unsafe = bad_proposal()
    refused_unsafe["alter_global_mission"] = True
    refuse(lambda: sup.transition(policy, state, refused_unsafe),
           "smuggling extra control fields")
    refused_no_map = copy.deepcopy(proposal)
    refused_no_map["evidence"].pop("functional_path")
    refuse(lambda: sup.transition(policy, original, refused_no_map),
           "mapping without functional chain")
    stale_revision = bad_proposal(
        to_status="INVESTIGATING", actor_role="scout")
    stale_revision["evidence"]["source_revision"] = "f" * 40
    refuse(lambda: sup.transition(policy, state, stale_revision),
           "proposed change against stale source revision")

    wrong = copy.deepcopy(state)
    wrong["mission_complete"] = True
    sup.write_atomic(sup.paths(root)[1], wrong)
    refuse(lambda: sup.load(root), "worker closing whole mission")

    wrong = copy.deepcopy(state)
    wrong["slices"] = wrong["slices"][:1]
    sup.write_atomic(sup.paths(root)[1], wrong)
    refuse(lambda: sup.load(root), "silently throwing away coverage")

    wrong = copy.deepcopy(state)
    wrong["slices"][0]["canonical_evidence_refs"] = []
    sup.write_atomic(sup.paths(root)[1], wrong)
    refuse(lambda: sup.load(root), "mapped stage missing durable evidence")

    wrong = copy.deepcopy(state)
    wrong["slices"][0]["audit_stage"] = "S5_SAME_INPUT_VERIFICATION"
    sup.write_atomic(sup.paths(root)[1], wrong)
    refuse(lambda: sup.load(root), "stage/status swap or lost task role")

    wrong = copy.deepcopy(state)
    wrong["slices"][0]["status"] = "VERIFIED"
    wrong["slices"][0]["verified"] = True
    sup.write_atomic(sup.paths(root)[1], wrong)
    refuse(lambda: sup.load(root), "fake verified result without evidence")

    sup.write_atomic(sup.paths(root)[1], state)
    original_policy = sup.paths(root)[0].read_text(encoding="utf-8")
    sup.paths(root)[0].write_text(
        original_policy.replace("breadth", "microtask", 1), encoding="utf-8")
    refuse(lambda: sup.load(root), "policy tamper, even with same mission ID")
    sup.paths(root)[0].write_text(original_policy, encoding="utf-8")
    assert fresh_process(script, root, "check").returncode == 0

print("ASAM_RESET_RECOVERY=PASS new-process context-free resume")
print("ASAM_MISSION_DRIFT_REJECTION=PASS global goal, coverage, evidence, role and epoch")
print("ASAM_CANONICAL_POINTERS=PASS existing MiniC docs, no new defect truth source")
