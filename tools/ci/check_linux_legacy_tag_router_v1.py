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
    "linux-expanded-pi-p1-runtime-v1.yml": (
        "9566e7d70e5ff0527e870b3b9ca6c06a715c8582",
        '  push:\n    branches:\n      - "agent/linux-expanded-kbuild-v0"\n',
        ("[linux-expanded-pi-runtime]", "[linux-expanded-runtime-p1]", "[linux-expanded-entry-trace]", "[linux-runtime-pi-local-symbol-v0]", "[runtime-compiler-semantics-v1]", "[linux-pi-micro]", "[linux-runtime-qemu-watch-cert-v1]", "[linux-runtime-qemu-watch-contracts-v1]", "[linux-runtime-qemu-watch-inconclusive-v1]"), "runtime-early",
    ),
    "linux-runtime-focused-faults-v1.yml": (
        "589b2f5e5c1d9d89b4e52c6e42faf1383e616f79",
        '  push:\n    branches:\n      - "agent/linux-expanded-kbuild-v0"\n',
        ("[linux-check-cpu-stall-runtime-v0]", "[linux-fork-stack-runtime-v0]", "[linux-runtime-fault-context-v1]", "[linux-runtime-satp-refresh-v0]", "[linux-runtime-frontier-v1]", "[linux-runtime-frontier-fast-v5]", "[linux-image-qemu-runtime-v0]"), "runtime-faults",
    ),
    "minild-integration-v1.yml": (
        "bb1e7ab8a728917324b652eba1fe1527efc1ee93",
        '  push:\n    branches:\n      - "agent/linux-expanded-kbuild-v0"\n      - "agent/linux-perf-boolean-domain-v1"\n',
        ("[minild-dynamic-integration]", "[minild-linux-rel-boundaries]", "[static-runtime]"),
        "minild-integrations",
    ),
    "linux-runtime-optin-perf-suite-v1.yml": (
        "3036af0be6310855d6b0319ca5cbe24516ad3a29",
        '  push:\n    branches: [agent/linux-expanded-kbuild-v0]\n    paths: [\'.github/workflows/linux-runtime-optin-perf-suite-v1.yml\']\n',
        ("[linux-perf3352]", "[linux-constant-p]", "[linux-perf500]"),
        "runtime-perf-suite",
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
        if name == "minild-integration-v1.yml":
            # Performance retains its original unscoped opt-in push; Runtime
            # must route through the same canonical owner via workflow_call.
            new_trigger = ("on:\n  push:\n    branches:\n"
                           '      - "agent/linux-perf-boolean-domain-v1"\n'
                           "  workflow_call:\n  workflow_dispatch:\n")
            header = current.split("\njobs:\n", 1)[0]
            assert new_trigger in header and "agent/linux-expanded-kbuild-v0" not in header
        else:
            new_trigger = "on:\n  workflow_call:\n  workflow_dispatch:\n"
            if "  push:" in current.split("\njobs:\n", 1)[0]:
                raise AssertionError(f"duplicate direct push listener: {name}")
        if current.count(new_trigger) != 1:
            raise AssertionError(f"reusable / manual events missing: {name}")
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
    # Every individual tag, every pair, all grouped tags, and an empty push.
    # Exhausting 2^22 combinations would be needless and slow for a T0 gate.
    subsets = [()] + [(tag,) for tag in all_tags] + list(combinations(all_tags, 2))
    subsets += [spec[2] for spec in OWNERS.values()] + [all_tags]
    seen = set()
    for subset in subsets:
        if subset in seen:
            continue
        seen.add(subset)
        msg = "normal commit " + " ".join(subset)
        observed = {spec[3] for spec in OWNERS.values()
                    if any(tag in msg for tag in spec[2])}
        expected = {spec[3] for spec in OWNERS.values()
                    if set(spec[2]).intersection(subset)}
        if observed != expected:
            raise AssertionError(f"tag routing mismatch: {subset}")
    print(f"M0_LEGACY_TAG_ROUTER=PASS owners={len(OWNERS)} old_push_tags={len(all_tags)} cases={len(seen)} "
          f"original_git_blobs={len(OWNERS)} no_test_body_changes=1 source_only=1")

if __name__ == "__main__":
    main()
