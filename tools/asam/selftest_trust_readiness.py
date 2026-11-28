#!/usr/bin/env python3
"""Trust-root negative acceptance; protection metadata NEVER auto-enables AI."""
import copy
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import trust_readiness as t

branch = {"name": t.BRANCH, "protected": False}
main = {"name": "main", "protected": False}
r = t.evaluate(branch, main, [])
assert r["status"] == "BLOCKED"
assert r["experiment_branch_protected"] is False
assert not r["autonomous_source_modification_enabled"]
assert not r["semantic_completion_gate_enabled"]
assert not r["independently_authorized_controller"]

# Even a stronger configured ruleset would NOT prove independent identity.
r2 = t.evaluate({"name": t.BRANCH, "protected": True},
                {"name": "main", "protected": True},
                [{"name": "some visible policy"}])
assert r2["status"] == "BLOCKED"
assert r2["experiment_branch_protected"] is True
assert not r2["verified_writer_identity"]
assert not r2["actual_independent_model_reset_accepted"]

for label, bad_branch, bad_main, bad_rules in [
    ("wrong branch", {"name": "main", "protected": True}, main, []),
    ("flag a string", {"name": t.BRANCH, "protected": "true"}, main, []),
    ("unknown main", branch, {"name": "other", "protected": False}, []),
    ("hidden rulesets", branch, main, None),
    ("no metadata", None, main, []),
]:
    try:
        t.evaluate(bad_branch, bad_main, bad_rules)
    except t.Blocked:
        pass
    else:
        raise AssertionError("untrusted protection state accepted: " + label)

print("ASAM_TRUST_FAIL_CLOSED=PASS branch flag alone never grants agent autonomy")
print("ASAM_TRUST_MUTATIONS=PASS 5 falsified settings rejected")
