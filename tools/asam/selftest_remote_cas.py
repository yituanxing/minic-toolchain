#!/usr/bin/env python3
"""Mutation tests for the REST-backed remote CAS proof. No writes or network."""
import copy
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import remote_cas_audit as audit

root = Path(__file__).resolve().parents[2]
probe = json.loads((root / "tools/asam/experiments/remote_cas_probe.json").read_text())
base_id = probe["base_commit"]
winner_id = probe["winner_commit"]
loser_id = probe["losing_candidate_commit"]
tree = probe["tree_sha"]
base = {"sha": base_id, "tree": {"sha": tree}, "parents": []}
winner = {"sha": winner_id, "tree": {"sha": tree},
          "parents": [{"sha": base_id}]}
loser = {"sha": loser_id, "tree": {"sha": tree},
         "parents": [{"sha": base_id}]}
ref = {"ref": "refs/heads/" + audit.BRANCH,
       "object": {"type": "commit", "sha": winner_id}}
compare = {
    "winner_to_tip": {"status": "identical", "ahead_by": 0, "behind_by": 0},
    "loser_to_tip": {"status": "diverged", "ahead_by": 1, "behind_by": 1},
    "winner_to_loser": {"status": "diverged", "ahead_by": 1, "behind_by": 1}
}
result = audit.evaluate(probe, base, winner, loser, ref, compare)
assert result["current_branch_excludes_losing_sibling"]
assert not result["concurrent_update_atomicity_formally_proved"]
assert not result["protected_independent_controller_implemented"]

def reject(label, change):
    p,b,w,l,r,c = map(copy.deepcopy,
                       (probe,base,winner,loser,ref,compare))
    change(p,b,w,l,r,c)
    try:
        audit.evaluate(p,b,w,l,r,c)
    except audit.Rejected:
        return
    raise AssertionError("CAS audit accepted " + label)

reject("wrong project", lambda p,b,w,l,r,c:
       p.update(repository="attacker/repo"))
reject("forced push", lambda p,b,w,l,r,c:
       p.update(force_used=True))
reject("write to main", lambda p,b,w,l,r,c:
       p.update(writes_to_main=True))
reject("same contender twice", lambda p,b,w,l,r,c:
       p.update(losing_candidate_commit=winner_id))
reject("loser different tree", lambda p,b,w,l,r,c:
       l["tree"].update(sha="f" * 40))
reject("base fake tree", lambda p,b,w,l,r,c:
       b["tree"].update(sha="f" * 40))
reject("loser is child of winner", lambda p,b,w,l,r,c:
       l["parents"][0].update(sha=winner_id))
reject("winner has extra parent", lambda p,b,w,l,r,c:
       w["parents"].append({"sha": loser_id}))
reject("branch swapped", lambda p,b,w,l,r,c:
       r.update(ref="refs/heads/main"))
reject("branch tip is losing sibling", lambda p,b,w,l,r,c:
       r["object"].update(sha=loser_id))
reject("winner no longer in branch", lambda p,b,w,l,r,c:
       c["winner_to_tip"].update(status="diverged", behind_by=1))
reject("loser merged into branch", lambda p,b,w,l,r,c:
       c["loser_to_tip"].update(status="ahead", behind_by=0))
reject("unrelated third contender", lambda p,b,w,l,r,c:
       c["winner_to_loser"].update(status="ahead", behind_by=0))
reject("missing DAG comparison", lambda p,b,w,l,r,c:
       c.pop("loser_to_tip"))
reject("wrong source hash", lambda p,b,w,l,r,c:
       w.update(sha="e" * 40))
reject("invalid probe SHA", lambda p,b,w,l,r,c:
       p.update(tree_sha="not-a-hash"))

for path in ("https://evil.invalid", "git/commits/../other",
             "git/ref/heads/main", "compare/123...abc"):
    assert audit.API_PART.fullmatch(path) is None, path

print("ASAM_REMOTE_CAS_MUTATIONS=PASS 16 wrong-DAG/forged-result cases rejected")
print("ASAM_REMOTE_CAS_SCOPE=PASS read-only verifier never authorizes semantic status")
