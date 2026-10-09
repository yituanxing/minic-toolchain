#!/usr/bin/env python3
"""Verify that the three MiniLD integration jobs survived convergence exactly.

Only their dispatch predicates and job-scoped concurrency may differ.
No GNU oracle, build, QEMU, timeout, artifact or assertion body may change.
"""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / ".github/workflows/minild-integration-v1.yml"
EXPECTED = {
    "dynamic-integration": ("minild-dynamic-integration-v1.yml", "9f28d3712c9a5aaea26b220d3de006d98e9d064c", "dynamic", "[minild-dynamic-integration]"),
    "linux-rel-boundaries": ("minild-linux-rel-boundaries-v1.yml", "826677e079536e2901b697cb81cc8caeb9e5f261", "linux-rel", "[minild-linux-rel-boundaries]"),
    "musl-lua-static": ("minild-static-runtime-v1.yml", "1b48c8a042d3ea2155fec45b40be6f48c375a40a", "static", "[static-runtime]"),
}

def split_jobs(text):
    if "\njobs:\n" not in text:
        raise AssertionError("jobs key absent")
    body = text.split("\njobs:\n", 1)[1]
    matches = list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$", body))
    result = {}
    for i, match in enumerate(matches):
        stop = matches[i + 1].start() if i + 1 < len(matches) else len(body)
        name = match.group(1)
        if name in result:
            raise AssertionError(f"duplicate job: {name}")
        result[name] = body[match.start():stop].strip()
    return result

def normalized(job):
    drop = (
        r"^    if: ",
        r"^    concurrency:$",
        r"^      group: minild-",
        r"^      cancel-in-progress: true$",
    )
    return "\n".join(
        line for line in job.splitlines()
        if not any(re.search(pattern, line) for pattern in drop)
    ).strip()

def main():
    current = WORK.read_text()
    jobs = split_jobs(current)
    if set(jobs) != set(EXPECTED):
        raise AssertionError(f"MiniLD job set differs: {sorted(jobs)}")
    if '      - "agent/linux-expanded-kbuild-v0"' not in current or '      - "agent/linux-perf-boolean-domain-v1"' not in current:
        raise AssertionError("current development branch triggers missing")
    for name, (filename, sha, mode, tag) in EXPECTED.items():
        old_path = ROOT / ".github/workflows-disabled" / filename
        if not old_path.is_file():
            raise AssertionError(f"missing archived source: {old_path}")
        actual = subprocess.check_output(["git", "hash-object", str(old_path)], text=True).strip()
        if actual != sha:
            raise AssertionError(f"archive blob changed: {filename}: {actual} != {sha}")
        original_jobs = split_jobs(old_path.read_text())
        if set(original_jobs) != {name}:
            raise AssertionError(f"old job identity differs: {filename}")
        if normalized(original_jobs[name]) != normalized(jobs[name]):
            raise AssertionError(f"changed test body: {name}")
        if f"inputs.mode == '{mode}'" not in jobs[name] or f"inputs.mode == 'all'" not in jobs[name]:
            raise AssertionError(f"dispatch selection absent: {name}")
        if f"contains(github.event.head_commit.message, '{tag}')" not in jobs[name]:
            raise AssertionError(f"original push tag absent: {name}")
        if f"group: {filename.removesuffix('.yml')}-" not in jobs[name]:
            raise AssertionError(f"job concurrency identity changed: {name}")
    print("M0_MINILD_INTEGRATION=PASS jobs=3 preserved_test_bodies=3 archived_blobs=3 current_branches=2")

if __name__ == "__main__":
    main()
