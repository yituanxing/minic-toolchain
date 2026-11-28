#!/usr/bin/env python3
"""Independent negative tests for GitHub REST CI metadata attestation.

No network needed; verifies that a green-looking but wrong repository,
head SHA, workflow, step or artifact is rejected. A separate CI step performs
a real live GitHub REST check of a completed historical run.
"""
import copy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import remote_evidence as gate

COMMIT = "9a223597ca5d44612561cf45c3a0b4f8ec0795c5"
RUN = 46106246261
BRANCH = "experiment/asam-supervisor-reset-20261128"
WORKFLOW = ".github/workflows/asam-supervisor-reset.yml"
JOB = "Verify permanent mission and fresh-process resume"
STEPS = [
    "Verify mission locks and all ten pending domains",
    "Test independent process restoration and forbidden transitions",
    "Index the full tracked compiler and export ten isolated work orders",
]
ARTIFACT = "asam-supervisor-fresh-context-packet"

run = {
    "id": RUN,
    "repository": {"full_name": gate.REPO},
    "head_sha": COMMIT,
    "head_branch": BRANCH,
    "event": "push",
    "status": "completed",
    "conclusion": "success",
    "path": WORKFLOW,
    "run_attempt": 1,
    "created_at": "2026-11-28T04:10:00Z",
    "updated_at": "2026-11-28T04:10:15Z",
}
job = {
    "id": 140859540274, "run_id": RUN, "head_sha": COMMIT,
    "name": JOB, "status": "completed", "conclusion": "success",
    "started_at": "2026-11-28T04:10:01Z",
    "completed_at": "2026-11-28T04:10:14Z",
    "steps": [
        {"name": n, "status": "completed", "conclusion": "success"}
        for n in STEPS
    ]
}
jobs = {"total_count": 1, "jobs": [job]}
artifacts = {
    "total_count": 1,
    "artifacts": [{
        "id": 15247237453, "name": ARTIFACT,
        "expired": False, "size_in_bytes": 100322,
        "workflow_run": {"id": RUN, "head_sha": COMMIT},
        "digest": "sha256:" + "a" * 64,
    }]
}
params = dict(run_id=RUN, commit=COMMIT, branch=BRANCH,
              workflow=WORKFLOW, job_name=JOB, steps=STEPS,
              artifact_name=ARTIFACT)
accepted = gate.evaluate(run, jobs, artifacts, **params)
assert accepted["evidence_kind"] == "GITHUB_REST_METADATA_ONLY"
assert accepted["semantic_authorization"] is False
assert accepted["trusted_runner_deployed_on_protected_ref"] is False
assert accepted["worker_mapping_approved"] is False


def reject(name, fn):
    r, j, a, p = (copy.deepcopy(run), copy.deepcopy(jobs),
                  copy.deepcopy(artifacts), copy.deepcopy(params))
    fn(r, j, a, p)
    try:
        gate.evaluate(r, j, a, **p)
    except gate.EvidenceRejected:
        return
    raise AssertionError("Forged GitHub evidence accepted: " + name)


reject("other repository", lambda r,j,a,p:
       r["repository"].update(full_name="another-owner/minic-toolchain"))
reject("wrong head sha", lambda r,j,a,p:
       r.update(head_sha="f" * 40))
reject("unrelated branch", lambda r,j,a,p:
       r.update(head_branch="main"))
reject("PR event", lambda r,j,a,p:
       r.update(event="pull_request"))
reject("queued run", lambda r,j,a,p:
       r.update(status="in_progress"))
reject("run failure", lambda r,j,a,p:
       r.update(conclusion="failure"))
reject("different workflow", lambda r,j,a,p:
       r.update(path=".github/workflows/unrelated.yml"))
reject("job sha wrong", lambda r,j,a,p:
       j["jobs"][0].update(head_sha="a" * 40))
reject("no required job", lambda r,j,a,p:
       j["jobs"][0].update(name="different job"))
reject("job skipped", lambda r,j,a,p:
       j["jobs"][0].update(conclusion="skipped"))
reject("step failed", lambda r,j,a,p:
       j["jobs"][0]["steps"][0].update(conclusion="failure"))
reject("step deleted", lambda r,j,a,p:
       j["jobs"][0]["steps"].pop())
reject("step duplicated", lambda r,j,a,p:
       j["jobs"][0]["steps"].append(copy.deepcopy(j["jobs"][0]["steps"][0])))
reject("job list incomplete", lambda r,j,a,p:
       j.update(total_count=2))
reject("artifact expired", lambda r,j,a,p:
       a["artifacts"][0].update(expired=True))
reject("artifact swapped run", lambda r,j,a,p:
       a["artifacts"][0]["workflow_run"].update(id=RUN+1))
reject("artifact swapped SHA", lambda r,j,a,p:
       a["artifacts"][0]["workflow_run"].update(head_sha="c" * 40))
reject("artifact empty", lambda r,j,a,p:
       a["artifacts"][0].update(size_in_bytes=0))
reject("artifact name changed", lambda r,j,a,p:
       a["artifacts"][0].update(name="unrelated.zip"))
reject("artifact paging incomplete", lambda r,j,a,p:
       a.update(total_count=2))
reject("run timestamp invalid", lambda r,j,a,p:
       r.update(updated_at="2026-11-28T04:09:00Z"))
reject("run lacks attempt", lambda r,j,a,p:
       r.update(run_attempt=None))
reject("job missing time", lambda r,j,a,p:
       j["jobs"][0].update(completed_at=None))

# Verify the REST API path construction cannot access another hostname/repo.
for invalid in (
    "https://attacker.invalid/",
    "/repos/another-owner/minic-toolchain/actions/runs/" + str(RUN),
    "/repos/" + gate.REPO + "/../another-repo",
):
    try:
        gate.api_read(invalid)
    except gate.EvidenceRejected:
        continue
    raise AssertionError("untrusted REST path accepted " + invalid)

print("ASAM_REMOTE_METADATA_REJECTION=PASS 23 forged run/job/step/artifact cases")
print("ASAM_REMOTE_API_DESTINATION=PASS untrusted paths blocked")
print("ASAM_REMOTE_EVIDENCE_ROLE=LIMITED metadata-only; never authorizes semantic work")
