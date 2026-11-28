#!/usr/bin/env python3
"""Read-only check of one *real* GitHub sibling-commit contention experiment.

This checks current GitHub ancestry and file trees, not a model's statement
that a write was rejected. It cannot authenticate the historical GraphQL
error reason, branch protection, or a distinct trusted reviewer.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
from urllib.error import URLError
from urllib.request import Request, urlopen

REPO = "yituanxing/minic-toolchain"
BRANCH = "experiment/asam-supervisor-reset-20261128"
SHA = re.compile(r"^[0-9a-f]{40}$")
API_PART = re.compile(
    r"^(?:git/commits/[a-f0-9]{40}|"
    r"git/ref/heads/experiment/asam-supervisor-reset-20261128|"
    r"compare/[a-f0-9]{40}\.\.\.[a-f0-9]{40})$"
)


class Rejected(ValueError):
    pass


def require(condition, reason):
    if not condition:
        raise Rejected(reason)


def get_json(part):
    require(API_PART.fullmatch(part) is not None,
            "unapproved GitHub REST path")
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "asam-remote-lease-audit",
    }
    token = os.environ.get("GH_TOKEN", "")
    if token:
        headers["Authorization"] = "Bearer " + token
    req = Request("https://api.github.com/repos/" + REPO + "/" + part,
                  headers=headers, method="GET")
    try:
        with urlopen(req, timeout=20) as response:
            require(response.status == 200, "non-200 GitHub result")
            raw = response.read(1024 * 1024 + 1)
            require(len(raw) < 1024 * 1024, "overlarge GitHub REST response")
            return json.loads(raw)
    except (URLError, OSError) as error:
        raise Rejected("GitHub REST unavailable, remote CAS not verified") from error


def evaluate(probe, base, win, lose, ref, compares):
    """Pass only the exact observed immutable sibling-DAG relation."""
    require(probe.get("schema") == "asam-remote-sibling-contention-v1" and
            probe.get("repository") == REPO and
            probe.get("branch") == BRANCH,
            "probe's repository or schema changed")
    required = ("base_commit", "winner_commit",
                "losing_candidate_commit", "tree_sha")
    require(all(isinstance(probe.get(k), str) and
                SHA.fullmatch(probe[k]) for k in required),
            "bad pinned commit/tree reference")
    parent = probe["base_commit"]
    winner = probe["winner_commit"]
    loser = probe["losing_candidate_commit"]
    tree = probe["tree_sha"]
    require(len({parent, winner, loser}) == 3,
            "reused parent or same contender in CAS experiment")
    require(probe["force_used"] is False and
            probe["writes_to_main"] is False,
            "test used forced or production reference writes")
    for obj, sha in ((base, parent), (win, winner), (lose, loser)):
        require(isinstance(obj, dict) and obj.get("sha") == sha and
                obj.get("tree", {}).get("sha") == tree,
                "candidate tree differs: source changes contaminate lease test")
    require(len(win.get("parents", [])) == 1 and
            win["parents"][0].get("sha") == parent and
            len(lose.get("parents", [])) == 1 and
            lose["parents"][0].get("sha") == parent,
            "remote candidates were not same-parent siblings")
    require(ref.get("ref") == "refs/heads/" + BRANCH and
            ref.get("object", {}).get("type") == "commit" and
            SHA.fullmatch(ref["object"].get("sha", "")),
            "wrong branch ref or invalid remote tip")
    tip = ref["object"]["sha"]
    require(set(compares) == {"winner_to_tip", "loser_to_tip",
                              "winner_to_loser"},
            "remote Git comparison evidence missing")
    wt = compares["winner_to_tip"]
    lt = compares["loser_to_tip"]
    wl = compares["winner_to_loser"]
    require(wt.get("status") in ("identical", "ahead") and
            wt.get("behind_by") == 0,
            "winning contender was no longer in current branch ancestry")
    require(lt.get("status") == "diverged" and
            lt.get("behind_by", 0) >= 1 and
            lt.get("ahead_by", 0) >= 1,
            "losing sibling was merged or became a descendant of remote tip")
    require(wl.get("status") == "diverged" and
            wl.get("ahead_by") == 1 and wl.get("behind_by") == 1,
            "candidate commits were not divergent one-commit siblings")
    return {
        "schema": "asam-remote-sibling-audit-v1",
        "branch": BRANCH,
        "remote_tip": tip,
        "original_parent": parent,
        "winning_candidate": winner,
        "losing_candidate": loser,
        "same_tree": tree,
        "current_branch_contains_winner": True,
        "current_branch_excludes_losing_sibling": True,
        "remote_nonforce_push_experiment_observed": True,
        "concurrent_update_atomicity_formally_proved": False,
        "specific_graphql_error_cause_authenticated": False,
        "protected_independent_controller_implemented": False,
        "semantic_completion_or_source_edit_permission": False,
        "caveats": [
            "Validates current GitHub ancestry; cannot prove no historical force-update.",
            "Generic GitHub GraphQL errors do not prove which check rejected a write.",
            "A single two-contender experiment is not a formal multi-host serializability proof.",
            "Only protected, dedicated controller may authorize future actual state writes."
        ]
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--probe", type=Path, default=Path(
        "tools/asam/experiments/remote_cas_probe.json"))
    p.add_argument("--output", type=Path,
                   default=Path("asam-supervisor-out/remote-cas-audit.json"))
    args = p.parse_args()
    try:
        probe = json.loads(args.probe.read_text(encoding="utf-8"))
        parent = probe["base_commit"]
        winner = probe["winner_commit"]
        loser = probe["losing_candidate_commit"]
        require(all(SHA.fullmatch(x or "") for x in (parent, winner, loser)),
                "invalid probe SHA")
        base_obj = get_json("git/commits/" + parent)
        a_obj = get_json("git/commits/" + winner)
        b_obj = get_json("git/commits/" + loser)
        ref = get_json("git/ref/heads/" + BRANCH)
        tip = ref.get("object", {}).get("sha", "")
        require(SHA.fullmatch(tip or "") is not None, "bad branch tip")
        comparisons = {
            "winner_to_tip": get_json("compare/" + winner + "..." + tip),
            "loser_to_tip": get_json("compare/" + loser + "..." + tip),
            "winner_to_loser": get_json("compare/" + winner + "..." + loser)
        }
        result = evaluate(probe, base_obj, a_obj, b_obj, ref, comparisons)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n",
                               encoding="utf-8")
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print("ASAM_REMOTE_CAS=REJECT " + str(exc), file=sys.stderr)
        return 2
    print("ASAM_REMOTE_CAS=PASS observed sibling non-overwrite: one winner, one unmerged")
    print("ASAM_REMOTE_CAS_TRUST=NOT_PROVED control ref still unprotected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
