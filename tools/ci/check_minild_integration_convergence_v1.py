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

# The original Linux REL wrapper matched only slash-prefixed output paths.
# Real Kbuild passes bare 'vmlinux.o', so the historical workflow was silently
# delegating that second boundary to GNU ld while the image still linked.
# Exactly this reviewed one-line execution repair is allowed; all other job
# bodies and archived Git blobs remain byte-exact.
OLD_REL_SELECTOR = "            */purgatory.ro|*/vmlinux.o)"
NEW_REL_SELECTOR = "            */purgatory.ro|purgatory.ro|*/vmlinux.o|vmlinux.o)"
OLD_REL_TRACE_ASSERT = "          grep -q '/vmlinux.o$' \"$trace\""
NEW_REL_TRACE_ASSERT = "          grep -Eq '(^|/)vmlinux[.]o$' \"$trace\""


def reverse_rel_selector_fix(text):
    if text.count(NEW_REL_SELECTOR) != 1 or OLD_REL_SELECTOR in text:
        raise AssertionError("MiniLD Linux REL selector repair missing or altered")
    if text.count(NEW_REL_TRACE_ASSERT) != 1 or OLD_REL_TRACE_ASSERT in text:
        raise AssertionError("MiniLD Linux REL trace assertion repair missing or altered")
    return (text.replace(NEW_REL_SELECTOR, OLD_REL_SELECTOR, 1)
                .replace(NEW_REL_TRACE_ASSERT, OLD_REL_TRACE_ASSERT, 1))


def main():
    current = WORK.read_text()
    jobs = split_jobs(current)
    if set(jobs) != set(EXPECTED):
        raise AssertionError(f"MiniLD job set differs: {sorted(jobs)}")
    head = current.split("\njobs:\n", 1)[0]
    expected_trigger = ('on:\n  push:\n    branches:\n'
                        '      - "agent/linux-perf-boolean-domain-v1"\n'
                        '  workflow_call:\n  workflow_dispatch:\n')
    if expected_trigger not in head or "agent/linux-expanded-kbuild-v0" in head:
        raise AssertionError("MiniLD must use Runtime router and preserve Performance direct push")
    # All executable job bodies and the historical full source can be recovered
    # exactly. In addition, check_linux_legacy_tag_router_v1.py checks this
    # original Git Blob on the Runtime ref.
    import hashlib
    original_trigger = ('on:\n  push:\n    branches:\n'
                        '      - "agent/linux-expanded-kbuild-v0"\n'
                        '      - "agent/linux-perf-boolean-domain-v1"\n'
                        '  workflow_dispatch:\n')
    recovered = reverse_rel_selector_fix(current).replace(expected_trigger, original_trigger, 1).encode()
    digest = hashlib.sha1(b"blob " + str(len(recovered)).encode()
                          + b"\0" + recovered).hexdigest()
    if digest != "bb1e7ab8a728917324b652eba1fe1527efc1ee93":
        raise AssertionError("MiniLD executable source changed beyond branch routing")
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
        normalized_current = normalized(jobs[name])
        if name == "linux-rel-boundaries":
            normalized_current = reverse_rel_selector_fix(normalized_current)
        if normalized(original_jobs[name]) != normalized_current:
            raise AssertionError(f"changed test body: {name}")
        if f"inputs.mode == '{mode}'" not in jobs[name] or f"inputs.mode == 'all'" not in jobs[name]:
            raise AssertionError(f"dispatch selection absent: {name}")
        if f"contains(github.event.head_commit.message, '{tag}')" not in jobs[name]:
            raise AssertionError(f"original push tag absent: {name}")
        if f"group: {filename.removesuffix('.yml')}-" not in jobs[name]:
            raise AssertionError(f"job concurrency identity changed: {name}")
    print("M0_MINILD_INTEGRATION=PASS jobs=3 preserved_test_bodies=3 archived_blobs=3 current_branches=2 approved_rel_selector_fix=1")

if __name__ == "__main__":
    main()
