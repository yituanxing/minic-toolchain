#!/usr/bin/env python3
"""Read-only GitHub REST evidence check. CI metadata is not semantic approval.

The reviewer must supply the expected repo, commit, workflow, branch, job,
steps and artifact. The tool fetches its OWN HTTPS API responses; it never
trusts a model-generated URL as evidence. GH_TOKEN is used for read-only API
rate limits in Actions. It is never written to outputs.
"""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import sys
import urllib.error
import urllib.request

REPO = "yituanxing/minic-toolchain"
HEX_SHA = re.compile(r"^[0-9a-f]{40}$")
RUN_ID = re.compile(r"^[1-9][0-9]{1,17}$")


class EvidenceRejected(ValueError):
    pass


def require(ok, reason):
    if not ok:
        raise EvidenceRejected(reason)


def api_read(path):
    require(path.startswith("/repos/" + REPO + "/") and
            "://" not in path and ".." not in path,
            "untrusted external API destination")
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "asam-evidence-gate/0.1",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    token = os.environ.get("GH_TOKEN", "")
    if token:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(
        "https://api.github.com" + path,
        headers=headers,
        method="GET"
    )
    try:
        with urllib.request.urlopen(request, timeout=18) as response:
            require(response.status == 200, "GitHub REST returned non-200")
            raw = response.read(1000000)
            require(len(raw) < 1000000, "GitHub response exceeds safe limit")
            return json.loads(raw)
    except (urllib.error.URLError, TimeoutError) as error:
        raise EvidenceRejected("GitHub REST unavailable; evidence NOT verified") from error


def parse_iso(value):
    require(isinstance(value, str) and value.endswith("Z"),
            "missing UTC completion time")
    try:
        return datetime.datetime.fromisoformat(
            value.replace("Z", "+00:00")
        )
    except ValueError as error:
        raise EvidenceRejected("invalid UTC time") from error


def evaluate(run, jobs, artifacts, *, run_id, commit, branch,
             workflow, job_name, steps, artifact_name):
    """Deterministic negative-testable verifier of fetched GitHub metadata."""
    require(isinstance(run, dict) and isinstance(jobs, dict)
            and isinstance(artifacts, dict), "malformed REST response")
    require(run.get("id") == run_id and
            run.get("repository", {}).get("full_name") == REPO,
            "run belongs to another repository or ID")
    require(run.get("head_sha") == commit and
            run.get("head_branch") == branch,
            "run does not match the EXACT pinned source/branch")
    require(run.get("event") == "push", "only pinned push CI evidence accepted")
    require(run.get("status") == "completed" and
            run.get("conclusion") == "success",
            "run has not independently completed successfully")
    wf = run.get("path", "").split("@", 1)[0].lstrip("/")
    require(wf == workflow, "different workflow file was executed")
    require(isinstance(run.get("run_attempt"), int) and
            run["run_attempt"] >= 1, "invalid run attempt")
    completed = parse_iso(run.get("updated_at"))
    started = parse_iso(run.get("created_at"))
    require(completed >= started, "inconsistent GitHub run timestamps")
    require(isinstance(jobs.get("jobs"), list)
            and jobs.get("total_count") == len(jobs["jobs"]),
            "incomplete job pagination or job listing")
    named = [j for j in jobs["jobs"] if j.get("name") == job_name]
    require(len(named) == 1, "required CI job missing or ambiguous")
    job = named[0]
    require(job.get("run_id") == run_id and
            job.get("head_sha") == commit and
            job.get("status") == "completed" and
            job.get("conclusion") == "success",
            "required job not completed on the pinned commit")
    require(job.get("started_at") is not None and
            job.get("completed_at") is not None and
            parse_iso(job["completed_at"]) >= parse_iso(job["started_at"]),
            "required job has no valid runtime provenance")
    all_steps = job.get("steps")
    require(isinstance(all_steps, list), "GitHub job step list unavailable")
    step_matches = {}
    for required in steps:
        match = [s for s in all_steps if s.get("name") == required]
        require(len(match) == 1 and match[0].get("status") == "completed"
                and match[0].get("conclusion") == "success",
                "required job step failed or was skipped: " + required)
        step_matches[required] = match[0]["conclusion"]
    require(isinstance(artifacts.get("artifacts"), list)
            and artifacts.get("total_count") == len(artifacts["artifacts"]),
            "incomplete artifact pagination or listing")
    matching = [a for a in artifacts["artifacts"] if
                a.get("name") == artifact_name]
    require(len(matching) == 1, "evidence artifact missing or ambiguous")
    artifact = matching[0]
    require(artifact.get("expired") is False and
            artifact.get("size_in_bytes", 0) > 0 and
            artifact.get("workflow_run", {}).get("id") == run_id and
            artifact.get("workflow_run", {}).get("head_sha") == commit,
            "artifact is expired, empty, or not tied to the pinned commit")
    return {
        "schema": "asam-github-ci-metadata-v1",
        "evidence_kind": "GITHUB_REST_METADATA_ONLY",
        "semantic_authorization": False,
        "worker_mapping_approved": False,
        "trusted_runner_deployed_on_protected_ref": False,
        "repo": REPO,
        "run_id": run_id, "run_attempt": run["run_attempt"],
        "branch": branch, "head_sha": commit,
        "workflow_path": wf, "job_id": job["id"],
        "job_name": job_name, "required_steps": step_matches,
        "artifact": {
            "id": artifact["id"], "name": artifact_name,
            "size_in_bytes": artifact["size_in_bytes"],
            "digest": artifact.get("digest"),
        },
        "limitations": [
            "A green GitHub run is not semantic equivalence or complete testing.",
            "This PR workflow is NOT a protected independent trust root.",
            "No model or remote identity is authenticated by self-declared role.",
            "The verifier itself must be executed from a protected trusted ref "
            "before its output can authorize MAPPED or VERIFIED."
        ]
    }


def verify(*, commit, branch, workflow, run_id, job_name, steps, artifact_name):
    require(HEX_SHA.fullmatch(commit) is not None, "bad commit SHA")
    require(RUN_ID.fullmatch(str(run_id)) is not None, "bad run ID")
    require(branch == "experiment/asam-supervisor-reset-20261128",
            "unapproved source branch")
    require(workflow == ".github/workflows/asam-supervisor-reset.yml",
            "unapproved workflow")
    prefix = "/repos/" + REPO + "/actions/runs/" + str(run_id)
    run = api_read(prefix)
    jobs = api_read(prefix + "/jobs?per_page=100")
    artifacts = api_read(prefix + "/artifacts?per_page=100")
    return evaluate(run, jobs, artifacts, run_id=run_id, commit=commit,
                    branch=branch, workflow=workflow,
                    job_name=job_name, steps=steps,
                    artifact_name=artifact_name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--commit", required=True)
    parser.add_argument("--run", type=int, required=True)
    parser.add_argument("--branch", default="experiment/asam-supervisor-reset-20261128")
    parser.add_argument("--workflow", default=".github/workflows/asam-supervisor-reset.yml")
    parser.add_argument("--job", default="Verify permanent mission and fresh-process resume")
    parser.add_argument("--step", action="append", required=True)
    parser.add_argument("--artifact", default="asam-supervisor-fresh-context-packet")
    parser.add_argument("--output", default="asam-supervisor-out/remote-ci-metadata.json")
    args = parser.parse_args()
    try:
        result = verify(commit=args.commit, branch=args.branch,
                        workflow=args.workflow, run_id=args.run,
                        job_name=args.job, steps=args.step,
                        artifact_name=args.artifact)
        dest = Path(args.output)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps(result, indent=2) + "\n",
                        encoding="utf-8")
    except (EvidenceRejected, OSError, ValueError, TypeError, KeyError) as error:
        print("ASAM_REMOTE_CI_EVIDENCE=REJECT " + str(error),
              file=sys.stderr)
        return 2
    print("ASAM_REMOTE_CI_METADATA=PASS run=" + str(args.run) +
          " (NOT semantic verification or a trusted reference)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
