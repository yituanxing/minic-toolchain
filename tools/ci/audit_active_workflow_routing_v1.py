#!/usr/bin/env python3
"""Audit active GitHub Actions entrypoints, real eligible push scope and skipped-run debt.

Pure source introspection; no GitHub API, network, or YAML dependency. Emit one
row per active workflow on the checked-out branch. This does NOT claim runtime
proof and does NOT change legacy commit-tag semantics.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
ACTIVE = ROOT / ".github" / "workflows"
DEBT_NAMES = (
    "linux-expanded-kbuild-v0.yml",
    "linux-expanded-pi-p1-runtime-v1.yml",
    "linux-runtime-fixture-producers-v1.yml",
    "linux-runtime-focused-faults-v1.yml",
    "miniar-linux-kbuild.yml",
    "minias-a0-gate-v1.yml",
    "minild-integration-v1.yml",
)
LEGACY_MANUAL = "minias-a0-focused-diagnostics-v1.yml"
SELF_YAML_ONLY = "linux-runtime-optin-perf-suite-v1.yml"
ROUTE_ALL_PUSH = "miniobjcopy-strip-regressions-v1.yml"
BRANCHES = ("agent/linux-expanded-kbuild-v0", "agent/linux-perf-boolean-domain-v1")
HEADER = (
    "branch", "workflow", "push_declared", "push_branch_eligible",
    "manual_dispatch", "push_path_policy", "jobs_declared", "job_ids",
    "commit_tag_guards", "unguarded_jobs", "dead_push_tags", "classification"
)


def trim_header(y: str) -> str:
    if "\njobs:\n" not in y:
        raise AssertionError("missing normal jobs section")
    return y.split("\njobs:\n", 1)[0]


def event_push(header: str) -> str | None:
    m = re.search(r"(?m)^  push:[ \t]*([^\n]*)\n?", header)
    if m is None:
        return None
    remainder = header[m.end():]
    next_event = re.search(r"(?m)^(?:  [a-z][a-z_-]*:|[a-z][a-z_-]*:)", remainder)
    return m.group(1) + "\n" + (remainder[:next_event.start()] if next_event else remainder)


def job_sections(y: str) -> dict[str, str]:
    tail = y.split("\njobs:\n", 1)[1]
    matches = list(re.finditer(r"(?m)^  ([a-zA-Z][\w-]*):\s*$", tail))
    if not matches:
        raise AssertionError("no jobs declared")
    return {m.group(1): tail[m.end():matches[i + 1].start() if i + 1 < len(matches) else len(tail)]
            for i, m in enumerate(matches)}


def analyze(name: str, y: str, branch: str) -> dict[str, str]:
    h = trim_header(y)
    p = event_push(h)
    push = p is not None
    if not push:
        path_policy = "none"
        eligible = False
    else:
        eligible = (branch in p) if re.search(r"(?m)^    branches:", p) else True
        if re.search(r"(?m)^    paths:\s*(?:\[|$)", p):
            path_policy = "scoped"
        elif re.search(r"(?m)^    paths-ignore:", p):
            path_policy = "ignored"
        else:
            path_policy = "unscoped"
    manual = bool(re.search(r"(?m)^  workflow_dispatch:", h))
    jobs = job_sections(y)
    guards = {key: re.findall(
        r"contains\(github\.event\.head_commit\.message,\s*['\"]([^'\"]+)['\"]\)",
        body) for key, body in jobs.items()}
    tags = sorted({t for sub in guards.values() for t in sub})
    unguarded = sorted(key for key, body in jobs.items()
                       if not re.search(r"(?m)^    if:", body))
    has_dead_tags = bool(tags and not push)
    if has_dead_tags:
        classification = "manual_only_unreachable_legacy_tag_checks"
    elif not eligible:
        classification = "manual_only_this_branch"
    elif path_policy == "unscoped" and name == ROUTE_ALL_PUSH:
        classification = "all_push_router_runner"
    elif path_policy == "unscoped" and tags and not unguarded:
        classification = "all_push_tag_gated_empty_run_candidate"
    elif path_policy == "unscoped" and tags:
        classification = "all_push_mixed_router_and_tags"
    elif path_policy == "unscoped":
        classification = "all_push_needs_review"
    elif name == SELF_YAML_ONLY:
        classification = "yaml_only_push_with_otherwise_unreachable_tags"
    elif path_policy == "scoped" and tags:
        classification = "path_scoped_tag_gated"
    else:
        classification = "source_scoped_or_manual"
    return {
        "branch": branch, "workflow": name,
        "push_declared": str(push).lower(),
        "push_branch_eligible": str(eligible).lower(),
        "manual_dispatch": str(manual).lower(),
        "push_path_policy": path_policy, "jobs_declared": str(len(jobs)),
        "job_ids": ";".join(jobs),
        "commit_tag_guards": ";".join(tags),
        "unguarded_jobs": ";".join(unguarded),
        "dead_push_tags": str(has_dead_tags).lower(),
        "classification": classification,
    }


def self_test():
    br = BRANCHES[0]
    r = analyze("sample.yml",
                "name: sample\non:\n  push:\n    branches: [agent/linux-expanded-kbuild-v0]\n"
                "  workflow_dispatch:\njobs:\n  example:\n    if: "
                "contains(github.event.head_commit.message, '[special]')\n"
                "    runs-on: ubuntu-24.04\n", br)
    assert r["push_branch_eligible"] == "true" and r["push_path_policy"] == "unscoped"
    assert r["classification"] == "all_push_tag_gated_empty_run_candidate"
    r = analyze("manual.yml",
                "on:\n  workflow_dispatch:\njobs:\n  diag:\n    if: "
                "contains(github.event.head_commit.message, '[old-tag]')\n"
                "    runs-on: ubuntu-24.04\n", br)
    assert r["dead_push_tags"] == "true"
    r = analyze("scoped.yml",
                "on:\n  push:\n    branches: [agent/linux-expanded-kbuild-v0]\n"
                "    paths: ['.github/workflows/scoped.yml']\n"
                "  workflow_dispatch:\njobs:\n  check:\n    runs-on: ubuntu-24.04\n", br)
    assert r["push_path_policy"] == "scoped"
    assert r["unguarded_jobs"] == "check"
    print("M0_CI_ENTRYPOINT_AUDIT_SELFTEST=PASS event_scope=3 branch=1 tag_guards=2")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="build/ci-active-entrypoint-audit.tsv")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    branch = __import__("os").environ.get("GITHUB_REF_NAME", BRANCHES[0])
    if branch not in BRANCHES:
        raise AssertionError(f"unsupported branch {branch}")
    rows = [analyze(f.name, f.read_text(), branch)
            for f in sorted(ACTIVE.glob("*.yml"))]
    expected = 25 if branch == BRANCHES[0] else 23
    if len(rows) != expected:
        raise AssertionError(f"inventory drift: branch {branch} YAML count {len(rows)} != {expected}")
    # Hard-coded high-risk names are only a diagnosis: their contract is frozen.
    names = {r["workflow"]: r for r in rows}
    for name in DEBT_NAMES:
        r = names[name]
        if branch == BRANCHES[0]:
            assert r["push_branch_eligible"] == "true", name
            assert r["push_path_policy"] == "unscoped", name
            assert r["commit_tag_guards"], name
    assert names[ROUTE_ALL_PUSH]["classification"] == "all_push_router_runner"
    assert names[LEGACY_MANUAL]["dead_push_tags"] == "true"
    assert names[SELF_YAML_ONLY]["classification"] == "yaml_only_push_with_otherwise_unreachable_tags" if branch == BRANCHES[0] else True
    out = ROOT / args.output
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=HEADER, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)
    cats: dict[str, int] = {}
    for row in rows:
        cats[row["classification"]] = cats.get(row["classification"], 0) + 1
    broad = [r["workflow"] for r in rows
             if r["classification"] == "all_push_tag_gated_empty_run_candidate"]
    print(f"M0_CI_ENTRYPOINT_AUDIT=PASS branch={branch} workflows={len(rows)} jobs={sum(int(r['jobs_declared']) for r in rows)}")
    print("CI_AUDIT_CLASS_COUNTS " + " ".join(f"{k}={v}" for k, v in sorted(cats.items())))
    print("CI_AUDIT_EMPTY_PUSH_CANDIDATES " + ",".join(broad))
    print(f"CI_AUDIT_OUTPUT={args.output} NOT_A_T1_T4_EXECUTION_CERTIFICATE")


if __name__ == "__main__":
    main()
