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

print("ASAM_GIT_HISTORY_NEGATIVE=PASS 16 rewritten/batched/fake-approved histories rejected")
print("ASAM_GIT_HISTORY_REAL=PASS read actual prior Git state revisions, not synthetic assumptions")
