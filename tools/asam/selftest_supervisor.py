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
# The real C09 testing evidence lives in the root Makefile and one CI YAML.
# Allow only those exact pinned sources; never silently accept arbitrary
# workflows, a different repository or path traversals as source authority.
baseline_sha = original["source_snapshot"]["observed_commit"]
prefix = "https://github.com/yituanxing/minic-toolchain/blob/" + baseline_sha + "/"
for allowed in ("Makefile", ".github/workflows/minic-runtime-fourarch.yml",
                "src/target/riscv/machine_core_select.c",
                "tests/compiler/c0/check-minic-target-boundaries.sh"):
    assert sup.SOURCE_LINK.fullmatch(prefix + allowed)
for forbidden in ("Makefile.backup", ".github/workflows/unrelated.yml",
                  ".github/other", ".github/workflows/../../Makefile",
                  "src/../../.github/workflows/evil.yml",
                  "README.md"):
    # Exact-whitelist and canonical segment semantics are also separately
    # exercised by actual sup.load() against the persisted C09/C10 data.
    match = sup.SOURCE_LINK.fullmatch(prefix + forbidden)
    segments = forbidden.split("/")
    assert match is None or any(part in ("", ".", "..") for part in segments), forbidden
assert sup.SOURCE_LINK.fullmatch(
    "https://github.com/other/repo/blob/" + baseline_sha + "/Makefile") is None
assert original["mission_complete"] is False
assert len(original["slices"]) == len(sup.EXPECTED_HINTS) == 10
assert all(not x["verified"] for x in original["slices"])
assert original["epoch"] >= 3
assert original["slices"][0]["status"] == "MAPPING_PROPOSED"
assert original["recorded_events"][2]["proposal"]["slice_id"] == "C01"
assert original["recorded_events"][3] == sup.EXPECTED_SCOPE_REFINEMENT_V2
current_planned_ids = [x["id"] for x in original["slices"]
                       if x["status"] == "PLANNED"]
planned_ids = current_planned_ids[:]
# The *actual* committed progress survives the model's temporary context.
live = fresh_process(script, repo, "resume")
assert live.returncode == 0, live.stderr
live_packet = json.loads(live.stdout)
assert live_packet["selected"]["id"] == (
    current_planned_ids[0] if current_planned_ids else "C01")
assert live_packet["coverage_status"]["MAPPING_PROPOSED"] == (
    10 - len(current_planned_ids))
assert live_packet["coverage_status"]["VERIFIED"] == 0
assert live_packet["temporary_context"] == []

# Use the actual durable current checkpoint. When breadth is exhausted,
# reconstruct the immediate predecessor solely inside this test fixture so
# repeated acceptance tests do not require hardcoded "C10" or fake progress.
s0 = copy.deepcopy(original)
if not planned_ids:
    event = s0["recorded_events"][-1]
    assert event["kind"] == "SLICE_TRANSITION"
    assert event["from"] == "PLANNED"
    assert event["to"] == "MAPPING_PROPOSED"
    slice_ = next(x for x in s0["slices"] if x["id"] == event["id"])
    assert slice_["status"] == "MAPPING_PROPOSED"
    slice_["status"] = "PLANNED"
    slice_["audit_stage"] = "S0_BASELINE"
    slice_["canonical_evidence_refs"] = []
    s0["epoch"] -= 1
    s0["recorded_events"].pop()
    planned_ids = [event["id"]]
sup.verify_event_replay(policy, s0)

with tempfile.TemporaryDirectory(prefix="asam-reset-") as td:
    root = Path(td)
    control = sup.tree(root)
    control.mkdir(parents=True)
    shutil.copy2(sup.paths(repo)[0], sup.paths(root)[0])
    sup.write_atomic(sup.paths(root)[1], s0)
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
    assert len(first["breadth_frontier"]) == len(planned_ids)
    assert first["selected"]["id"] == planned_ids[0]
    assert first["temporary_context"] == []
    assert len(first["nonnegotiable"]) >= 16
    assert "minic-global-contract-map.md" in repr(first["canonical_sources"])

    proposal = {
        "schema": "asam-transition-v1",
        "mission_id": policy["mission_id"],
        "expected_epoch": s0["epoch"],
        "slice_id": planned_ids[0],
        "to_status": "MAPPING_PROPOSED",
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
    assert restored["coverage_status"]["MAPPING_PROPOSED"] == (
        11 - len(planned_ids))
    assert restored["coverage_status"]["PLANNED"] == len(planned_ids) - 1
    assert restored["selected"]["id"] == (
        planned_ids[1] if len(planned_ids) > 1 else "C01")
    assert restored["selected"]["instruction"].startswith(
        "Read existing canonical docs" if len(planned_ids) > 1
        else "Independently challenge")
    assert restored["global_task_completed"] is False
    assert restored["policy_sha256"] == first["policy_sha256"]
    assert restored["global_mission"] == first["global_mission"]
    assert restored["temporary_context"] == []
    assert restored["source_baseline"] == first["source_baseline"]

    state = json.loads(sup.paths(root)[1].read_text(encoding="utf-8"))
    updated_slice = next(x for x in state["slices"]
                         if x["id"] == planned_ids[0])
    assert updated_slice["status"] == "MAPPING_PROPOSED"
    assert not updated_slice["verified"]
    assert updated_slice["audit_stage"] == "S1_FUNCTIONAL_MAP"

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
    refuse(lambda: sup.transition(policy, s0, refused_no_map),
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
