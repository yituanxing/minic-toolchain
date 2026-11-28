#!/usr/bin/env python3
"""ASAM Git-history audit: reject state.json rewrites that bypass event deltas.

This independent checker reads *every committed version* of state.json from
the Git DAG, not just the latest self-consistent JSON snapshot. It is a
read-only CI gate, NOT an authenticated remote controller. A writer able to
rewrite both the branch history and this verifier remains out of scope.
"""
import argparse
import copy
import json
from pathlib import Path
import re
import subprocess
import sys

STATE_PATH = "tools/asam/control/state.json"
MISSION = "ASAM-MINIC-GLOBAL-20261128"
SHA = re.compile(r"^[0-9a-f]{40}$")
FIXED_FIELDS = (
    "schema", "policy_id", "policy_sha256", "global_status",
    "mission_complete", "project", "source_snapshot", "schedule_mode",
    "work_limit", "active_task", "guidance"
)
ISSUE_KIND = "SLICE_TRANSITION"
PERMITTED_STATUS = "MAPPING_PROPOSED"


class ChainRejected(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise ChainRejected(message)


def git(root, *args):
    process = subprocess.run(
        ["git", *args], cwd=root, check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    require(process.returncode == 0, "git read failed: " +
            process.stderr.decode("utf-8", "replace")[:300])
    return process.stdout.decode("utf-8", "replace")


def verify_initial(state):
    require(state.get("epoch") == 0 and
            state.get("policy_id") == MISSION and
            state.get("schema") == "asam-control-state-v1" and
            state.get("global_status") == "ACTIVE" and
            state.get("mission_complete") is False,
            "invalid original S0 mission/epoch")
    events = state.get("recorded_events")
    require(isinstance(events, list) and len(events) == 1 and
            events[0].get("kind") == "HISTORICAL_EXEMPLAR_NOT_GLOBAL_COMPLETION",
            "origin event history is inconsistent")
    slices = state.get("slices")
    require(isinstance(slices, list) and len(slices) == 9 and
            [s.get("id") for s in slices] ==
            ["C" + str(i).zfill(2) for i in range(1, 10)],
            "initial source scope was substituted")
    require(all(s.get("status") == "PLANNED" and
                s.get("audit_stage") == "S0_BASELINE" and
                s.get("verified") is False and
                s.get("canonical_evidence_refs") == [] and
                s.get("last_verified_revision") is None
                for s in slices), "origin was falsely reviewed")
    require(slices[0]["scope_hint"] ==
            ["tools/minic/main.c", "tools/minic-cc/main.c"],
            "initial Driver scope was rewritten")


def check_transition(before, after, *, kind):
    require(after["epoch"] == before["epoch"] + 1,
            "state epoch was skipped or reset")
    before_events, after_events = (before["recorded_events"],
                                   after["recorded_events"])
    require(after_events[:len(before_events)] == before_events and
            len(after_events) == len(before_events) + 1,
            "old evidence was overwritten, truncated or events were batched")
    event = after_events[-1]
    require(event.get("kind") == kind and
            event.get("epoch") == after["epoch"],
            "wrong event kind or event epoch")
    require(all(before.get(k) == after.get(k) for k in FIXED_FIELDS),
            "global mission, source or scheduling authority was rewritten")
    require(set(before) == set(after),
            "unexpected state metadata added or removed")
    return event


def check_s0_migration(before, after):
    event = check_transition(before, after, kind="S0_SCOPE_REFINEMENT")
    require(before["epoch"] == 0 and after["epoch"] == 1,
            "S0 region addition at unexpected time")
    require(event.get("added") == ["C10"] and
            event.get("refined") == ["C01"] and
            event.get("expected_unassigned_after_refinement") == 0,
            "S0 scope migration changed from observed facts")
    old, new = before["slices"], after["slices"]
    require(len(new) == len(old) + 1 and
            new[9]["id"] == "C10" and new[9]["status"] == "PLANNED" and
            new[9]["verified"] is False and
            new[9]["scope_hint"] == ["src/common/"],
            "C10 was added incorrectly")
    require(old[1:] == new[1:-1],
            "unrelated regions changed with S0 migration")
    def without_scope_and_note(data):
        return {k: v for k, v in data.items()
                if k not in ("scope_hint", "note")}
    require(without_scope_and_note(old[0]) ==
            without_scope_and_note(new[0]) and
            new[0]["scope_hint"] == ["tools/minic/", "tools/minic-cc/"],
            "S0 migration altered Driver semantics or forged progress")


def check_mapping_proposal(before, after):
    event = check_transition(before, after, kind=ISSUE_KIND)
    require(event.get("from") == "PLANNED" and
            event.get("to") == PERMITTED_STATUS,
            "an untrusted mapping was promoted to trusted coverage")
    ident = event.get("id")
    original_ids = [s["id"] for s in before["slices"]]
    modified_ids = [s["id"] for s in after["slices"]]
    require(original_ids == modified_ids and
            len(set(modified_ids)) == len(modified_ids),
            "a task slice was duplicated, inserted, deleted or reordered")
    old = {s["id"]: s for s in before["slices"]}
    new = {s["id"]: s for s in after["slices"]}
    require(set(old) == set(new) and ident in old and
            old[ident]["status"] == "PLANNED",
            "mapping result references unknown or already changed slice")
    proposal = event.get("proposal")
    require(isinstance(proposal, dict) and
            proposal.get("schema") == "asam-transition-v1" and
            proposal.get("mission_id") == MISSION and
            proposal.get("expected_epoch") == before["epoch"] and
            proposal.get("slice_id") == ident and
            proposal.get("to_status") == PERMITTED_STATUS and
            proposal.get("actor_role") == "mapper" and
            proposal.get("actor_id") == event.get("actor_id"),
            "state update bypassed proposal protocol")
    evidence = proposal.get("evidence", {})
    require(isinstance(evidence, dict) and
            evidence.get("source_revision") ==
            before["source_snapshot"]["observed_commit"] and
            evidence.get("independent_review", False) is False and
            evidence.get("semantic_coverage_claimed", False) is False,
            "untrusted Mapper falsely claimed a review or wrong revision")
    ref = evidence.get("link")
    require(isinstance(ref, str) and
            ref == "https://github.com/yituanxing/minic-toolchain/blob/" +
            evidence["source_revision"] + "/" + evidence.get("canonical_ref", ""),
            "untrusted canonical record pointer was swapped")
    require(event.get("source") == ref and
            event.get("actor_id") == proposal["actor_id"],
            "event and proposal have different identities")
    expected = copy.deepcopy(old[ident])
    expected["status"] = PERMITTED_STATUS
    expected["audit_stage"] = "S1_FUNCTIONAL_MAP"
    expected["canonical_evidence_refs"].append(ref)
    require(new[ident] == expected,
            "slice changed outside proposed untrusted stage")
    for key in old:
        if key != ident:
            require(old[key] == new[key],
                    "other functional region changed during Mapper proposal")


def check_s0_expansion(before, after):
    event = check_transition(before, after, kind="S0_SCOPE_REFINEMENT_V2")
    require(before["epoch"] == 2 and after["epoch"] == 3,
            "S0 v2 scope migration out of sequence")
    require(event.get("observed_repository_c_h") == 1983 and
            event.get("previous_scanned_c_h") == 1746 and
            event.get("new_in_scope_c_h") == 153 and
            event.get("external_c_h_excluded") == 84 and
            event.get("verified_slices") == 0,
            "S0 v2 migration changed its observed scope counts")
    require(event.get("external_prefixes") ==
            ["archiver/", "assembler/", "elf/", "linker/",
             "preprocessor/", "third_party/"],
            "third-party exclusion changed")
    original_ids = [s["id"] for s in before["slices"]]
    modified_ids = [s["id"] for s in after["slices"]]
    require(original_ids == modified_ids and
            len(set(modified_ids)) == len(modified_ids),
            "S0 v2 duplicated, removed or reordered functional slice")
    before_slices = {s["id"]: s for s in before["slices"]}
    after_slices = {s["id"]: s for s in after["slices"]}
    require(set(before_slices) == set(after_slices),
            "S0 v2 lost or added functional region")
    hints = {
        "C01": ["tools/minic/", "tools/minic-cc/", "tools/minic-"],
        "C09": ["tests/"],
        "C10": ["src/common/", "include/minic/"]
    }
    require(event.get("scope_changes") == hints,
            "S0 v2 migration event does not match scope deltas")
    for key in before_slices:
        a, b = before_slices[key], after_slices[key]
        if key in hints:
            require(b["scope_hint"] == hints[key] and
                    {x: v for x, v in a.items() if x not in ("scope_hint", "note")} ==
                    {x: v for x, v in b.items() if x not in ("scope_hint", "note")},
                    "S0 v2 scope migration changed task authority: " + key)
        else:
            require(a == b, "unrelated region changed in S0 v2")


def check_s0_scope_v3(before, after):
    """Independently check the real C06/C07 source-boundary correction.

    In particular, an already proposed C01-C05 must not lose status,
    evidence, or task identity when the scope coverage is corrected.
    """
    event = check_transition(before, after, kind="S0_SCOPE_REFINEMENT_V3")
    require(before["epoch"] == 7 and after["epoch"] == 8,
            "third scope migration at unexpected epoch")
    baseline = before["source_snapshot"]["observed_commit"]
    require(event.get("source_baseline") == baseline and
            event.get("verified_slices") == 0,
            "scope migration asserts wrong source baseline or review status")
    old_scope = {
        "C06": ["src/target/common/"],
        "C07": ["src/target/x86_64/", "src/target/aarch64/",
                "src/target/riscv/", "src/target/loongarch64/"]
    }
    revised = {
        "C06": ["src/target/common/", "src/target/source_policy/",
                "src/target/data_layout.h", "src/target/data_layout_source.h",
                "src/target/target_info.h"],
        "C07": ["src/target/x86_64/", "src/target/aarch64/",
                "src/target/riscv32/", "src/target/riscv64/",
                "src/target/loongarch64/"]
    }
    require(event.get("prior_scope") == old_scope and
            event.get("scope_changes") == revised and
            event.get("observed_paths") == [
                "src/target/source_policy/language_semantics.c",
                "src/target/source_policy/data_layout_source.c",
                "src/target/common/core_function_plan.c",
                "src/target/riscv32/rv32_emit.c",
                "src/target/riscv64/codegen_function.c"
            ] and event.get("rationale") ==
            "C06 had omitted source-policy and language DataLayout; C07 pointed to nonexistent src/target/riscv/ instead of separate real RV32 and RV64 backends. This migrates candidate boundaries, NOT semantic review or verification.",
            "third migration changes boundaries without full recorded justification")
    before_slices, after_slices = before["slices"], after["slices"]
    require([x["id"] for x in before_slices] ==
            [x["id"] for x in after_slices] and
            len(before_slices) == len(after_slices) == 10,
            "third migration changed functional region identity/order")
    for old, new in zip(before_slices, after_slices):
        ident = old["id"]
        if ident in revised:
            require(old["scope_hint"] == old_scope[ident] and
                    new["scope_hint"] == revised[ident] and
                    {k: v for k, v in old.items() if k != "scope_hint"} ==
                    {k: v for k, v in new.items() if k != "scope_hint"},
                    "third migration secretly changed semantic authority: " + ident)
        else:
            require(old == new,
                    "third migration changed unrelated coverage: " + ident)
    require(all(x["status"] == "PLANNED" for x in after_slices[5:7]),
            "third migration retroactively certified C06/C07")


def check_s0_scope_v4(before, after):
    """Correct the V3 omitted *existing* shared RISC-V machine selector."""
    event = check_transition(before, after, kind="S0_SCOPE_REFINEMENT_V4")
    require(before["epoch"] == 8 and after["epoch"] == 9 and
            event.get("source_baseline") ==
            before["source_snapshot"]["observed_commit"] and
            event.get("verified_slices") == 0,
            "fourth scope migration not at pinned S0 state")
    old = ["src/target/x86_64/", "src/target/aarch64/",
           "src/target/riscv32/", "src/target/riscv64/",
           "src/target/loongarch64/"]
    new = ["src/target/x86_64/", "src/target/aarch64/",
           "src/target/riscv/", "src/target/riscv32/",
           "src/target/riscv64/", "src/target/loongarch64/"]
    require(event.get("prior_scope") == {"C07": old} and
            event.get("scope_changes") == {"C07": new} and
            event.get("observed_paths") == [
                "src/target/riscv/machine_core_select.c",
                "src/target/riscv/machine_core_select.h",
                "src/target/riscv/machine_target_state.h",
                "src/target/riscv/machine_gnu_asm.c",
                "src/target/riscv32/rv32_emit.c",
                "src/target/riscv64/codegen_function.c"
            ] and event.get("rationale") ==
            "Correct S0-v3 false assertion: src/target/riscv/ is a real shared RISC-V machine implementation that BOTH RV32 and RV64 consume. V3 correctly added separate ABI directories but incorrectly removed their common selector. Restore all three directories, retain the historical error and never claim review completion.",
            "shared RISC-V machine implementation correction was forged")
    before_rows, after_rows = before["slices"], after["slices"]
    require(len(before_rows) == len(after_rows) == 10 and
            [x["id"] for x in before_rows] ==
            [x["id"] for x in after_rows],
            "fourth migration changed task count, order, or identity")
    for a, b in zip(before_rows, after_rows):
        if a["id"] == "C07":
            require(a["scope_hint"] == old and b["scope_hint"] == new and
                    {k: v for k, v in a.items() if k != "scope_hint"} ==
                    {k: v for k, v in b.items() if k != "scope_hint"} and
                    a["status"] == "PLANNED",
                    "fourth migration changed unreviewed C07 authority")
        else:
            require(a == b,
                    "fourth migration changed an unrelated function area")


def verify_versions(versions):
    require(len(versions) >= 4, "incomplete original state history")
    verify_initial(versions[0])
    for i, (before, after) in enumerate(zip(versions, versions[1:]), 1):
        if i == 1:
            check_s0_migration(before, after)
        elif i == 3:
            check_s0_expansion(before, after)
        elif i == 8:
            check_s0_scope_v3(before, after)
        elif i == 9:
            check_s0_scope_v4(before, after)
        else:
            check_mapping_proposal(before, after)
    require(versions[-1]["epoch"] == len(versions) - 1,
            "latest state has unexplained hidden history revisions")
    return len(versions) - 1


def history(root):
    commits = git(root, "log", "--reverse", "--format=%H", "HEAD",
                  "--", STATE_PATH).splitlines()
    require(commits and all(SHA.fullmatch(x) for x in commits),
            "state path Git history missing")
    versions = []
    for sha in commits:
        changed = git(root, "diff-tree", "--no-commit-id", "--name-only",
                      "-r", "--root", sha).splitlines()
        require(STATE_PATH in changed, "state version lacks changed file")
        require(len(changed) == 1, "state was modified alongside executable " +
                "or other files: " + sha)
        content = git(root, "show", sha + ":" + STATE_PATH)
        versions.append(json.loads(content))
    return commits, versions


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        commits, versions = history(root)
        count = verify_versions(versions)
        current = json.loads((root / STATE_PATH).read_text(encoding="utf-8"))
        require(current == versions[-1],
                "checkout state does not equal last committed state event")
    except (ChainRejected, OSError, ValueError, KeyError, TypeError) as exc:
        print("ASAM_GIT_STATE_HISTORY=REJECT " + str(exc), file=sys.stderr)
        return 2
    print("ASAM_GIT_STATE_HISTORY=PASS " + str(count) +
          " independent single-event state commits checked")
    print("ASAM_UNTRUSTED_STATE_PROMOTION=DISABLED no semantic approval")
    return 0


if __name__ == "__main__":
    sys.exit(main())
