#!/usr/bin/env python3
"""T0: prove legacy two-owner push-tag routing without modifying original jobs.

GitHub workflow_call inherits the caller's push event payload and ref. The two
costly jobs remain guarded by their original exact commit-message predicates.
This check certifies source equivalence, not real Linux build or QEMU behavior.
"""
from pathlib import Path
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
        "[linux-expanded-kbuild-v0]", "linux-kbuild",
    ),
    "miniar-linux-kbuild.yml": (
        "9fdb2ff5e44e2a46b137ea7e0d5e11afc4b51310",
        '  push:\n    branches:\n      - "agent/linux-expanded-kbuild-v0"\n',
        "[miniar-linux-kbuild]", "miniar-kbuild",
    ),
}

def git_blob(payload: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(payload)).encode() + b"\0" + payload).hexdigest()

def main():
    branch = os.environ.get("GITHUB_REF_NAME", RUNTIME)
    if branch not in (RUNTIME, PERFORMANCE):
        raise AssertionError("unknown audited branch: " + branch)
    if branch == PERFORMANCE:
        if ROUTER.exists():
            raise AssertionError("Runtime-only opt-in router leaked onto Performance")
        print("M0_LEGACY_TAG_ROUTER=PASS branch=Performance route=Runtime-only")
        return
    route = ROUTER.read_text()
    if not re.search(r'(?m)^  push:\n    branches: \["agent/linux-expanded-kbuild-v0"\]', route):
        raise AssertionError("Runtime router lost one push event listener")
    if "  workflow_dispatch:" in route:
        raise AssertionError("router must not advertise an unverified default-branch manual entrypoint")
    observed = set(re.findall(r"(?m)^  ([a-z][\w-]*):\s*$", route.split("\njobs:\n", 1)[1]))
    # Special-tag-only probe is temporary and must be removed after a real green run.
    assert "route-smoke" in observed
    assert "    if: contains(github.event.head_commit.message, '[ci-legacy-router-smoke-v1]')" in route
    assert "    uses: ./.github/workflows/toolchain-m0-structure.yml" in route
    if observed != set(owner[3] for owner in OWNERS.values()) | {"route-smoke"}:
        raise AssertionError(f"router job coverage drift {observed}")
    for name, (sha, old_push, tag, caller) in OWNERS.items():
        active = (ROOT / ".github/workflows" / name).read_text()
        if active.count("on:\n  workflow_call:\n  workflow_dispatch:\n") != 1:
            raise AssertionError("reusable/manual triggers missing for " + name)
        if "  push:" in active.split("\njobs:\n", 1)[0]:
            raise AssertionError("duplicate direct push remains on " + name)
        restored = active.replace("on:\n  workflow_call:\n  workflow_dispatch:\n",
                                  "on:\n" + old_push + "  workflow_dispatch:\n", 1)
        if git_blob(restored.encode()) != sha:
            raise AssertionError("original execution and manual contract modified for " + name)
        expected_if = f"    if: contains(github.event.head_commit.message, '{tag}')"
        expected_uses = f"    uses: ./.github/workflows/{name}"
        job = re.search(r"(?ms)^  " + re.escape(caller) + r":\n(.*?)(?=^  [a-z][\w-]*:\s*$|\Z)", route)
        if not job or expected_if not in job.group(1) or expected_uses not in job.group(1):
            raise AssertionError("router must call only on original tag " + name)
    for tags in ((), ("[linux-expanded-kbuild-v0]",),
                 ("[miniar-linux-kbuild]",),
                 ("[linux-expanded-kbuild-v0]", "[miniar-linux-kbuild]")):
        message = "unrelated text " + " ".join(tags)
        selected = {name for name, (_, _, tag, _) in OWNERS.items() if tag in message}
        if len(selected) != len(tags):
            raise AssertionError(f"push tag coverage changed: {tags}")
    print("M0_LEGACY_TAG_ROUTER=PASS owners=2 old_push_tags=2 original_git_blobs=2 no_test_body_changes=1 source_only=1")

if __name__ == "__main__":
    main()
