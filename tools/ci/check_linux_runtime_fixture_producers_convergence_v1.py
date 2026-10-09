#!/usr/bin/env python3
"""Certify original frozen and compact fixture producers survived convergence.

Every build, cache key, checksum, artifact upload and real oracle step is
unchanged. Only the dispatch selector and workflow->job concurrency move differ.
"""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / ".github/workflows/linux-runtime-fixture-producers-v1.yml"
SPECS = {
    "frozen-cert": (
        "linux-runtime-frozen-cert-v1.yml",
        "00508c60c5f17ad0e4433f4b369c0b5eaef9de7a",
        "frozen", "[linux-runtime-frozen-cert-v1]",
        "linux-runtime-frozen-cert-v1",
    ),
    "compact-cert": (
        "linux-runtime-link-fixture-cert-v1.yml",
        "dbdf655f18123a786b4f8fadeb7328b87dd2a954",
        "compact", "[linux-runtime-link-fixture-cert-v1]",
        "linux-runtime-link-fixture-cert-v1",
    ),
}

def jobs(text):
    section = text.split("\njobs:\n", 1)[1]
    matches = list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$", section))
    return {
        match.group(1): section[match.start():matches[i+1].start() if i+1<len(matches) else len(section)].strip()
        for i, match in enumerate(matches)
    }

def nonselector_body(text):
    return "\n".join(
        line for line in text.splitlines()
        if not re.search(
            r"^    if: |^    concurrency:$|^      group: linux-runtime-(?:frozen-cert-v1|link-fixture-cert-v1)-|^      cancel-in-progress: true$",
            line,
        )
    ).strip()

def main():
    current = WORK.read_text()
    active = jobs(current)
    if set(active) != set(SPECS):
        raise AssertionError(f"producer job IDs drifted: {active.keys()}")
    if '"agent/linux-expanded-kbuild-v0"' not in current:
        raise AssertionError("original push branch eligibility lost")
    for name, (filename, git_blob, mode, tag, group) in SPECS.items():
        old = ROOT / ".github/workflows-disabled" / filename
        actual = subprocess.check_output(["git", "hash-object", str(old)], text=True).strip()
        if actual != git_blob:
            raise AssertionError(f"original producer YAML SHA changed: {filename}")
        prior = jobs(old.read_text())
        if set(prior) != {name} or nonselector_body(prior[name]) != nonselector_body(active[name]):
            raise AssertionError(f"producer's checksum, cache or executable step altered: {name}")
        actual_job = active[name]
        if f"inputs.mode == '{mode}'" not in actual_job or "github.event_name == 'workflow_dispatch'" not in actual_job:
            raise AssertionError(f"missing named manual mode: {name}")
        if f"contains(github.event.head_commit.message, '{tag}')" not in actual_job:
            raise AssertionError(f"lost original tag trigger: {name}")
        if f"group: {group}-" not in actual_job or "cancel-in-progress: true" not in actual_job:
            raise AssertionError(f"original independent cancellation policy altered: {name}")
    if not ("          - frozen" in current and "          - compact" in current):
        raise AssertionError("both certified producer selection modes must be available")
    print("M0_RUNTIME_FIXTURE_PRODUCERS=PASS jobs=2 original_cache_and_certification_bodies=2 archived_original_sha=2")

if __name__ == "__main__":
    main()
