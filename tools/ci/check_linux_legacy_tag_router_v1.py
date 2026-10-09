#!/usr/bin/env python3
"""Source gate for historical Runtime commit-tag push routing.

The Runtime router calls original job bodies through workflow_call, preserving
push payload and the original commit message. On 2026-10-09 a REAL push-tagged
probe confirmed reusable call execution and push-context inheritance:
https://github.com/yituanxing/minic-toolchain/actions/runs/37902327902
This gate itself is T0, not Linux Image / QEMU / 3536 test certification.
"""
from pathlib import Path
from itertools import combinations
import hashlib
import os
import re

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = "agent/linux-expanded-kbuild-v0"
PERFORMANCE = "agent/linux-perf-boolean-domain-v1"
ROUTER = ROOT / ".github/workflows/linux-legacy-tag-router-v1.yml"

OWNERS = {
    "linux-expanded-kbuild-v0.yml": (
        "35500d29ab940d0033832d50b17e33e0d9c2f9db",
        '  push:\n    branches: ["agent/linux-expanded-kbuild-v0"]\n',
        ("[linux-expanded-kbuild-v0]",), "linux-kbuild",
    ),
    "miniar-linux-kbuild.yml": (
        "9fdb2ff5e44e2a46b137ea7e0d5e11afc4b51310",
        '  push:\n    branches:\n      - "agent/linux-expanded-kbuild-v0"\n',
        ("[miniar-linux-kbuild]",), "miniar-kbuild",
    ),
    "linux-runtime-fixture-producers-v1.yml": (
        "1bdabc68a2c8fb60e86b79ed6757fc6157e8914c",
        '  push:\n    branches: ["agent/linux-expanded-kbuild-v0"]\n',
        ("[linux-runtime-frozen-cert-v1]", "[linux-runtime-link-fixture-cert-v1]"),
        "linux-fixture",
    ),
    "minias-a0-gate-v1.yml": (
        "f430ce905fa4c49715ce4cd5ef1dc4cef2054f9c",
        '  push:\n    branches:\n      - "agent/linux-expanded-kbuild-v0"\n',
        ("[minias-gate-v1]", "[minias-native-repro]"), "minias-gate",
    ),
}

def git_blob(payload: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(payload)).encode() + b"\0" + payload).hexdigest()

def main():
    branch = os.environ.get("GITHUB_REF_NAME", RUNTIME)
    if branch not in (RUNTIME, PERFORMANCE):
        raise AssertionError(f"unexpected branch: {branch}")
    if branch == PERFORMANCE:
        if ROUTER.exists():
            raise AssertionError("Runtime-only opt-in router leaked onto Performance")
        print("M0_LEGACY_TAG_ROUTER=PASS branch=Performance scope=Runtime-only")
        return
    route = ROUTER.read_text()
    header, tail = route.split("\njobs:\n", 1)
    if not re.search(r'(?m)^  push:\n    branches: \["agent/linux-expanded-kbuild-v0"\]', header):
        raise AssertionError("router Runtime push branch lost")
    if "  workflow_dispatch:" in header or "  workflow_call:" in header:
        raise AssertionError("router must be a single push entrypoint")
    matches = list(re.finditer(r"(?m)^  ([a-z][\w-]*):\s*$", tail))
    actual = {m.group(1): tail[m.end():matches[i+1].start() if i+1 < len(matches) else len(tail)]
              for i, m in enumerate(matches)}
    expected_ids = {spec[3] for spec in OWNERS.values()}
    if set(actual) != expected_ids:
        raise AssertionError(f"unexpected router jobs: {set(actual) ^ expected_ids}")
    for name, (original_sha, push_block, tags, job_id) in OWNERS.items():
        current = (ROOT / ".github/workflows" / name).read_text()
        new_trigger = "on:\n  workflow_call:\n  workflow_dispatch:\n"
        if current.count(new_trigger) != 1:
            raise AssertionError(f"reusable / manual events missing: {name}")
        if "  push:" in current.split("\njobs:\n", 1)[0]:
            raise AssertionError(f"duplicate direct push listener: {name}")
        reconstructed = current.replace(new_trigger,
                                         "on:\n" + push_block + "  workflow_dispatch:\n", 1)
        if git_blob(reconstructed.encode()) != original_sha:
            raise AssertionError(f"original job bodies or manual behavior changed: {name}")
        expected_cond = "    if: " + " || ".join(
            f"contains(github.event.head_commit.message, '{tag}')" for tag in tags)
        expected_use = f"    uses: ./.github/workflows/{name}"
        job = actual[job_id].strip().splitlines()
        if job != [expected_cond.strip(), expected_use]:
            raise AssertionError(f"router job has unreviewed conditions or arguments: {job_id}")
    all_tags = tuple(tag for spec in OWNERS.values() for tag in spec[2])
    checks = 0
    for count in range(len(all_tags)+1):
        for subset in combinations(all_tags, count):
            msg = "normal commit " + " ".join(subset)
            observed = {spec[3] for spec in OWNERS.values()
                        if any(tag in msg for tag in spec[2])}
            expected = {spec[3] for spec in OWNERS.values()
                        if set(spec[2]).intersection(subset)}
            if observed != expected:
                raise AssertionError(f"tag routing mismatch: {subset}")
            checks += 1
    print(f"M0_LEGACY_TAG_ROUTER=PASS owners=4 old_push_tags=6 cases={checks} "
          "original_git_blobs=4 no_test_body_changes=1 source_only=1")

if __name__ == "__main__":
    main()
