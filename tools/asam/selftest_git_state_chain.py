#!/usr/bin/env python3
"""Negative tests for ASAM state *history*, not only final JSON snapshots."""
import copy
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import git_state_chain as audit

root = Path(__file__).resolve().parents[2]
commits, actual = audit.history(root)
assert len(actual) >= 4, "expected original S0 + two migrations + C01"
assert audit.verify_versions(actual) >= 3


def reject(label, transform):
    variants = copy.deepcopy(actual)
    transform(variants)
    try:
        audit.verify_versions(variants)
    except audit.ChainRejected:
        return
    raise AssertionError("unsafe historical change accepted: " + label)


reject("delete original proposal history",
       lambda v: v[2]["recorded_events"].pop())
reject("invent verified C01 outcome",
       lambda v: v[2]["slices"][0].update(
           status="VERIFIED", verified=True))
reject("change another domain while updating C01",
       lambda v: v[2]["slices"][1].update(status="MAPPING_PROPOSED"))
reject("replace old migration evidence",
       lambda v: v[3]["recorded_events"][1].update(
           expected_unassigned_after_refinement=9))
reject("raise task epoch without adding a transition",
       lambda v: v[2].update(epoch=99))
reject("make S0 v2 silently mark C01 verified",
       lambda v: v[3]["slices"][0].update(verified=True))
reject("switch source baseline during progress",
       lambda v: v[2]["source_snapshot"].update(
           observed_commit="f" * 40))
reject("smuggle extra state metadata",
       lambda v: v[1].update(worker_claim="all fixed"))
reject("truncate an old event by keeping the epoch",
       lambda v: v[3]["recorded_events"].pop(0))
reject("falsify outside project scope",
       lambda v: v[3]["recorded_events"][-1].update(
           external_c_h_excluded=0))
reject("create additional C11 without migration",
       lambda v: v[2]["slices"].append(copy.deepcopy(v[2]["slices"][9])))
reject("remove C10 during rebaseline",
       lambda v: v[3]["slices"].pop())
reject("change canonical source evidence reference",
       lambda v: v[2]["slices"][0].update(canonical_evidence_refs=[
           "https://github.com/other/project"]))
reject("replace event actor identity",
       lambda v: v[2]["recorded_events"][-1].update(
           actor_id="supervisor-owner"))
reject("introduce copied optimization removal as a fake scope event",
       lambda v: v[3]["recorded_events"][-1].update(
           scope_changes={"C07": ["src/target/x86_64/"]}))
reject("bulk duplicate the last event",
       lambda v: v[3]["recorded_events"].append(
           copy.deepcopy(v[3]["recorded_events"][-1])))

# The later migration must be a one-file, one-event, pure candidate-scope
# correction. Preserve all prior provisional progress and never create an
# imaginary riscv/ backend by rewriting history.
assert len(actual) >= 9 and actual[8]["recorded_events"][-1]["kind"] == "S0_SCOPE_REFINEMENT_V3"
reject("replace v3 candidate C07 with phantom backend",
       lambda v: v[8]["slices"][6].update(
           scope_hint=["src/target/x86_64/", "src/target/riscv/"]))
reject("silently advance C07 during v3 migration",
       lambda v: v[8]["slices"][6].update(
           status="MAPPING_PROPOSED", audit_stage="S1_FUNCTIONAL_MAP"))
reject("rewrite previous C04 proposal on v3 update",
       lambda v: v[8]["slices"][3].update(
           canonical_evidence_refs=["https://github.com/attacker/rewrite"]))
reject("rewrite v3 observed RV64 evidence",
       lambda v: v[8]["recorded_events"][-1]["observed_paths"].remove(
           "src/target/riscv64/codegen_function.c"))
reject("drop source-policy from v3 scope event",
       lambda v: v[8]["recorded_events"][-1]["scope_changes"]["C06"].remove(
           "src/target/source_policy/"))
reject("v3 record lies about verified progress",
       lambda v: v[8]["recorded_events"][-1].update(
           verified_slices=1))

# V4 must preserve the misclassification in V3's immutable history while
# restoring the original shared selector as a new independently audited event.
assert len(actual) >= 10 and actual[9]["recorded_events"][-1]["kind"] == "S0_SCOPE_REFINEMENT_V4"
reject("V4 still drops shared RISC-V selector",
       lambda v: v[9]["slices"][6]["scope_hint"].remove(
           "src/target/riscv/"))
reject("V4 deletes the earlier incorrect V3 event",
       lambda v: v[9]["recorded_events"].pop(8))
reject("V4 claims certification for the unreviewed selector",
       lambda v: v[9]["slices"][6].update(verified=True))
reject("V4 silently rewrites unrelated C06 path",
       lambda v: v[9]["slices"][5]["scope_hint"].remove(
           "src/target/source_policy/"))
reject("V4 source trace incorrectly attributed to other target",
       lambda v: v[9]["recorded_events"][-1]["observed_paths"].append(
           "src/target/nonexistent/machine_core_select.c"))
reject("V4 corrupts old source provenance",
       lambda v: v[9]["recorded_events"][-1].update(
           source_baseline="f" * 40))

print("ASAM_GIT_HISTORY_NEGATIVE=PASS 28 rewritten/batched/fake-approved histories rejected")
print("ASAM_GIT_HISTORY_REAL=PASS read actual prior Git state revisions, not synthetic assumptions")
