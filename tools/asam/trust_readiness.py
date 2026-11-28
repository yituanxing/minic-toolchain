#!/usr/bin/env python3
"""Read-only GitHub trust-root check: protection is necessary, not sufficient.

This file intentionally has NO branch-protection or ruleset mutation method.
The active GitHub connector is not authorized to grant itself trust.
"""
import argparse
import json
import os
from pathlib import Path
import sys
from urllib.error import URLError
from urllib.request import Request, urlopen

REPO = "yituanxing/minic-toolchain"
BRANCH = "experiment/asam-supervisor-reset-20261128"
PATHS = {
    "experiment": "/repos/" + REPO + "/branches/" + BRANCH,
    "main": "/repos/" + REPO + "/branches/main",
    "rulesets": "/repos/" + REPO + "/rulesets",
}


class Blocked(ValueError):
    pass


def read(kind):
    path = PATHS[kind]
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "asam-trust-observer",
    }
    token = os.environ.get("GH_TOKEN")
    if token:
        headers["Authorization"] = "Bearer " + token
    req = Request("https://api.github.com" + path,
                  method="GET", headers=headers)
    try:
        with urlopen(req, timeout=15) as response:
            if response.status != 200:
                raise Blocked("GitHub trust settings not readable")
            raw = response.read(1024 * 1024)
            return json.loads(raw)
    except (URLError, OSError) as exc:
        raise Blocked("GitHub protection settings unavailable") from exc


def evaluate(experiment, main, rulesets):
    if not isinstance(experiment, dict) or not isinstance(main, dict):
        raise Blocked("branch protection metadata missing")
    if not isinstance(rulesets, list):
        raise Blocked("rulesets inventory unavailable")
    if (experiment.get("name") != BRANCH or main.get("name") != "main"
            or type(experiment.get("protected")) is not bool
            or type(main.get("protected")) is not bool):
        raise Blocked("wrong branch or unverifiable protection flag")
    return {
        "schema": "asam-trust-readiness-observation-v1",
        "repo": REPO,
        "experiment_branch_protected": experiment["protected"],
        "main_branch_protected": main["protected"],
        "visible_ruleset_count": len(rulesets),
        # A protected branch does not prove approvals, bypass limits,
        # signer trust, code-owners or distinct human/model identities.
        "independently_authorized_controller": False,
        "verified_writer_identity": False,
        "actual_independent_model_reset_accepted": False,
        "remote_cas_only_is_sufficient": False,
        "semantic_completion_gate_enabled": False,
        "autonomous_source_modification_enabled": False,
        "status": "BLOCKED",
        "required_next_actions": [
            "Obtain a dedicated, independently maintained controller repository",
            "Apply reviewed branch ruleset requiring owners and verified CI",
            "Restrict write credentials to the controller, never the worker",
            "Verify protected writer identity and GitHub CI attestation",
            "Run an actual independent fresh-LLM rehydration and challenge",
        ],
        "limitations": [
            "Branch 'protected' metadata is a coarse flag, not proof of rule strength.",
            "Visible repository rulesets may not include all organizational policies.",
            "This observer cannot create or attest any access-control rule.",
            "GitHub experiment branch and its CI remain untrusted."
        ]
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path,
                   default=Path("asam-supervisor-out/trust-readiness.json"))
    args = p.parse_args()
    try:
        result = evaluate(read("experiment"), read("main"), read("rulesets"))
    except (ValueError, KeyError, TypeError, OSError) as exc:
        print("ASAM_TRUST_OBSERVATION=UNAVAILABLE " + str(exc), file=sys.stderr)
        return 2
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n",
                           encoding="utf-8")
    print("ASAM_TRUST_BRANCH_PROTECTED=" +
          str(result["experiment_branch_protected"]).lower())
    print("ASAM_TRUST_MAIN_PROTECTED=" +
          str(result["main_branch_protected"]).lower())
    print("ASAM_TRUST_VISIBLE_RULESETS=" + str(result["visible_ruleset_count"]))
    print("ASAM_TRUST_AUTONOMY=BLOCKED independent controller not established")
    return 0


if __name__ == "__main__":
    sys.exit(main())
