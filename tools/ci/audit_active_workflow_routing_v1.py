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
LEGACY_MANUAL = "minias-a0-focused-diagnostics-v1.yml"
SELF_YAML_ONLY = "linux-runtime-optin-perf-suite-v1.yml"
ROUTE_ALL_PUSH = "miniobjcopy-strip-regressions-v1.yml"
CENTRAL_TAG_ROUTER = "linux-legacy-tag-router-v1.yml"
# Performance-only dormant entrypoints; original Git blobs are retained verbatim.
PERFORMANCE_ARCHIVED = {
    "linux-expanded-kbuild-v0.yml": "35500d29ab940d0033832d50b17e33e0d9c2f9db",
    "linux-expanded-pi-p1-runtime-v1.yml": "2d09b865c9e69ef0266c53f5e2c64cd3fb1b1fd3",
    "miniar-linux-kbuild.yml": "9fdb2ff5e44e2a46b137ea7e0d5e11afc4b51310",
    "linux-runtime-spinlock-context-v0.yml": "5dde74694e43401ab95f93bf3fcc5d8120b322f5",
    "linux-core-shards-v1.yml": "cd18cb3b43541ef7d825045743b2e0e752e62763",
    "linux-runtime-fdt-isolation-v1.yml": "b9c56ef805cb78b711ef1f9059f69fc276838893",
    "linux-runtime-fixture-producers-v1.yml": "1bdabc68a2c8fb60e86b79ed6757fc6157e8914c",
    "linux-runtime-focused-faults-v1.yml": "589b2f5e5c1d9d89b4e52c6e42faf1383e616f79",
    "linux-runtime-focused-owners-v1.yml": "6ad661f9e8614d2740477640cb2fe0de905d6c40",
    "linux-runtime-owner-focused-v1.yml": "f751fe83c49947302201e9cd09d313a865735372",
}
# Performance branch has a historical MiniAS definition with a different blob:
# its old cleanup-only push branch is irrelevant on Performance. Preserve both
# originals and never substitute the Runtime version silently.
MINIAS_GATE_VARIANTS = {
    "agent/linux-expanded-kbuild-v0": "d0256dd6e28be76bc43f8cf2bc3296277748de99",
    "agent/linux-perf-boolean-domain-v1": "be679f45cd1bf81bfefca7fee0cc12aeb95aa802",
}


# Two Performance archival snapshots still pin the original unmodified owner
# sources, while their Runtime live counterparts are now reusable workflows.
RUNTIME_REUSABLE_BLOBS = {
    "linux-expanded-kbuild-v0.yml": "fd613d04769765af9f0047b3f800c2d13a4bc579",
    "linux-expanded-pi-p1-runtime-v1.yml": "c5ac17f907ac115c20b5e1cb4f547f025118353f",
    "miniar-linux-kbuild.yml": "b06aef58b69c0566cefa0a1751298f88926f2529",
    "linux-runtime-fixture-producers-v1.yml": "ef54b8d6551eb84d663f0a5be3d1f1f811dccaf0",
    "linux-runtime-focused-faults-v1.yml": "fde303682a2e14c1697b7daf6017c9144773deac",
    "linux-runtime-spinlock-context-v0.yml": "6355ccea22ecf495654bf71b8668adb1306eb04f",
    "linux-runtime-fdt-isolation-v1.yml": "11c03a87850710a055e8080a73f79acb91f14518",
}

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
    reusable = bool(re.search(r"(?m)^  workflow_call:", h))
    jobs = job_sections(y)
    guards = {key: re.findall(
        r"contains\(github\.event\.head_commit\.message,\s*['\"]([^'\"]+)['\"]\)",
        body) for key, body in jobs.items()}
    tags = sorted({t for sub in guards.values() for t in sub})
    unguarded = sorted(key for key, body in jobs.items()
                       if not re.search(r"(?m)^    if:", body))
    # A job guard reading the inherited caller push payload is LIVE under
    # on.workflow_call. It is dead only when neither push nor call is declared.
    has_dead_tags = bool(tags and not push and not reusable)
    if has_dead_tags:
        classification = "manual_only_unreachable_legacy_tag_checks"
    elif reusable and not eligible:
        classification = "reusable_tagged_dispatch"
    elif not eligible:
        classification = "manual_only_this_branch"
    elif eligible and name == CENTRAL_TAG_ROUTER and path_policy == "unscoped":
        classification = "centralized_optin_tag_router"
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



def assert_control_scoped_tag_reachable(name: str, source: str, branch: str) -> None:
    """Prevent tag-only diagnostic owners hidden by control-file-only push paths.

    GitHub evaluates on.push.paths *before* a job's commit-message if. A file
    change to src/ plus a matching historical tag cannot reach a workflow whose
    only paths are sentinel trigger files or its own YAML. Keep the scoped push
    for historical sentinel probes, but require a separately audited reusable
    entrypoint for such tag guards.
    """
    observed = analyze(name, source, branch)
    if observed["push_branch_eligible"] != "true" or observed["push_path_policy"] != "scoped":
        return
    if not observed["commit_tag_guards"]:
        return
    h = trim_header(source)
    scoped_push = event_push(h)
    if scoped_push is None:
        return
    if "/**" not in scoped_push and not re.search(r"(?m)^  workflow_call:", h):
        raise AssertionError(
            f"unreachable push tag: {name} only listens to control-file paths, "
            "but contains commit-message opt-in guards and lacks workflow_call"
        )


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
    r = analyze("reusable.yml",
                "on:\n  workflow_call:\n  workflow_dispatch:\njobs:\n  owner:\n"
                "    if: contains(github.event.head_commit.message, '[legacy]')\n"
                "    runs-on: ubuntu-24.04\n", br)
    assert r["dead_push_tags"] == "false" and r["classification"] == "reusable_tagged_dispatch"
    self_only = ("on:\n  push:\n    branches: [agent/linux-expanded-kbuild-v0]\n"
                 "    paths: ['.github/workflows/demo.yml']\n"
                 "  workflow_dispatch:\njobs:\n  check:\n"
                 "    if: contains(github.event.head_commit.message, '[demo]')\n"
                 "    runs-on: ubuntu-24.04\n")
    try:
        assert_control_scoped_tag_reachable("demo.yml", self_only, br)
    except AssertionError:
        pass
    else:
        raise AssertionError("control-only tag without route was accepted")
    assert_control_scoped_tag_reachable(
        "demo.yml", self_only.replace("  workflow_dispatch:", "  workflow_call:\n  workflow_dispatch:"), br
    ) is None
    print("M0_CI_ENTRYPOINT_AUDIT_SELFTEST=PASS event_scope=4 branch=1 "
          "tagged_reusable=1 hidden_control_tag_rejected=1")


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
    # The source-controlled manifest defines active owner identities. No
    # duplicated 26/13 magic counts are needed in independent audits.
    inventory = ROOT / "docs/ci/active-workflow-inventory-2026-10-09.tsv"
    with inventory.open(newline="") as f:
        owned = {Path(v["path"]).name for v in csv.DictReader(f, delimiter="\t")
                 if v["branch"] == branch}
    actual_names = {v["workflow"] for v in rows}
    if not owned or actual_names != owned:
        raise AssertionError(f"active owner drift from single manifest: missing={owned-actual_names} extra={actual_names-owned}")
    names = {r["workflow"]: r for r in rows}
    # This is an executable guardrail, not merely a report. Never allow a new
    # broad push listener that silently creates hundreds of empty run cards.
    unscoped = {r["workflow"] for r in rows
                if r["push_branch_eligible"] == "true"
                and r["push_path_policy"] == "unscoped"}
    expected_unscoped = ({CENTRAL_TAG_ROUTER, ROUTE_ALL_PUSH}
                         if branch == BRANCHES[0]
                         else {ROUTE_ALL_PUSH, "minild-integration-v1.yml"})
    if unscoped != expected_unscoped:
        raise AssertionError(f"unreviewed broad push entrypoints: {unscoped ^ expected_unscoped}")
    for name in names:
        assert_control_scoped_tag_reachable(name, (ACTIVE / name).read_text(), branch)
    # Keep every retired Performance owner as an exact byte-identical archive.
    # These entries must remain intact and branch-ineligible on Performance.
    import hashlib
    # Two preexisting historical archives share filenames with retired copies.
    # Both original historical sources remain pinned and must NOT be overwritten.
    historical = {
        "linux-runtime-fdt-isolation-v1.yml": "83feffda9150072a849f677c58856f77975968cc",
        "linux-expanded-pi-p1-runtime-v1.yml": ("acd84a8de23ebf908f75eb863df285cfb83b79c0"
            if branch == BRANCHES[0] else "0dff72daea890b37972c64a6411caa18b00cf528"),
        "linux-runtime-focused-faults-v1.yml": "770c2bb3c8831b35d7eabebbc8dba8d09dc32384",
    }
    for name, historical_sha in historical.items():
        original = ROOT / ".github" / "workflows-disabled" / name
        assert original.is_file(), f"lost historical archive {name}"
        old_bytes = original.read_bytes()
        old_hash = hashlib.sha1(b"blob " + str(len(old_bytes)).encode() + b"\0" + old_bytes).hexdigest()
        assert old_hash == historical_sha, f"altered historical archive {name}"
    for name, expected_sha in PERFORMANCE_ARCHIVED.items():
        live = ACTIVE / name
        retired_dir = ROOT / ".github" / "workflows-disabled"
        if name in historical:
            retired_dir = retired_dir / "performance-retired-2026-10-09"
        archive = retired_dir / name
        if branch == BRANCHES[0]:
            assert live.is_file(), name
            payload = live.read_bytes()
        else:
            assert not live.exists() and archive.is_file(), name
            payload = archive.read_bytes()
            observed = analyze(name, payload.decode("utf-8"), branch)
            assert observed["push_branch_eligible"] == "false", name
        digest = hashlib.sha1(b"blob " + str(len(payload)).encode() + b"\0" + payload).hexdigest()
        pinned_sha = RUNTIME_REUSABLE_BLOBS.get(name, expected_sha) if branch == BRANCHES[0] else expected_sha
        assert digest == pinned_sha, f"Performance/Runtime owner source drift: {name}"
    # MiniAS gate is still live for Runtime. Preserve Performance's historically
    # branch-ineligible variant byte-for-byte, not the newer Runtime variant.
    mini_name = "minias-a0-gate-v1.yml"
    mini_live = ACTIVE / mini_name
    mini_archive = ROOT / ".github/workflows-disabled/performance-retired-2026-10-09" / mini_name
    if branch == BRANCHES[0]:
        assert mini_live.is_file(), mini_name
        mini_bytes = mini_live.read_bytes()
    else:
        assert not mini_live.exists() and mini_archive.is_file(), mini_name
        mini_bytes = mini_archive.read_bytes()
        mini_meta = analyze(mini_name, mini_bytes.decode("utf-8"), branch)
        assert mini_meta["push_branch_eligible"] == "false", mini_name
        assert mini_meta["jobs_declared"] == "5", mini_name
        assert "workflow_call:" not in trim_header(mini_bytes.decode("utf-8")), mini_name
    mini_actual = hashlib.sha1(b"blob " + str(len(mini_bytes)).encode() + b"\0" + mini_bytes).hexdigest()
    assert mini_actual == MINIAS_GATE_VARIANTS[branch], f"MiniAS branch-specific gate source drift: {branch}"
    if branch == BRANCHES[0]:
        assert names[CENTRAL_TAG_ROUTER]["classification"] == "centralized_optin_tag_router"
        for called in ("linux-expanded-kbuild-v0.yml", "miniar-linux-kbuild.yml",
                       "linux-runtime-fixture-producers-v1.yml", "minias-a0-gate-v1.yml",
                       "linux-expanded-pi-p1-runtime-v1.yml", "linux-runtime-focused-faults-v1.yml"):
            assert names[called]["push_declared"] == "false"
            assert names[called]["manual_dispatch"] == "true"
    else:
        assert CENTRAL_TAG_ROUTER not in names
    ld = names["minild-integration-v1.yml"]
    assert ld["manual_dispatch"] == "true"
    assert ld["push_declared"] == "true"
    assert ld["push_branch_eligible"] == ("false" if branch == BRANCHES[0] else "true")
    assert names[ROUTE_ALL_PUSH]["classification"] == "all_push_router_runner"
    assert names[LEGACY_MANUAL]["push_declared"] == "false"
    assert names[LEGACY_MANUAL]["manual_dispatch"] == "true"
    assert names[LEGACY_MANUAL]["commit_tag_guards"] == ""
    # The opt-in performance suite used to be YAML-self-path only and its
    # expensive [linux-perf*] labels were unreachable on normal source pushes.
    # It is now a reusable owner behind the same audited Runtime tag router.
    if branch == BRANCHES[0]:
        assert names[SELF_YAML_ONLY]["classification"] == "reusable_tagged_dispatch"
        assert names[SELF_YAML_ONLY]["push_declared"] == "false"

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
