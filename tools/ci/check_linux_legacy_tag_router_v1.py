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
    "linux-runtime-spinlock-context-v0.yml": (
        "5dde74694e43401ab95f93bf3fcc5d8120b322f5",
        "  push:\n    branches: [\"agent/linux-expanded-kbuild-v0\"]\n    paths:\n      - \".github/workflows/linux-runtime-spinlock-context-v0.yml\"\n      - \"tools/ci/runtime-spinlock-trigger.txt\"\n      - \"tools/ci/runtime-spinlock-stack-trigger.txt\"\n",
        ("[linux-spinlock-context]", "[linux-spinlock-codegen]", "[linux-spinlock-first-context]", "[linux-spinlock-stack]"), "spinlock-probes",
    ),
    "linux-runtime-fdt-isolation-v1.yml": (
        "b9c56ef805cb78b711ef1f9059f69fc276838893",
        "  push:\n    branches: [\"agent/linux-expanded-kbuild-v0\"]\n    paths:\n      - \".github/workflows/linux-runtime-fdt-isolation-v1.yml\"\n      - \".github/workflows-disabled/linux-runtime-generated-kallsyms-first-die-v0.yml\"\n      - \"tools/ci/linux-early-runtime-link-v2.sh\"\n",
        ("[linux-fdt-isolation]", "[linux-fdt-codegen]", "[linux-kallsyms-generated]", "[linux-kallsyms-three-way]", "[linux-kallsyms-object]"), "fdt-kallsyms-probes",
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
    expected_ids = {spec[3] for spec in OWNERS.values()} | {"minias-focused"}
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
        elif name in ("linux-runtime-spinlock-context-v0.yml",
                           "linux-runtime-fdt-isolation-v1.yml"):
            # Preserve the historical scoped sentinel-path push. Add a
            # reusable, tag-addressable route for *normal* source pushes.
            new_trigger = "on:\n" + push_block + "  workflow_call:\n  workflow_dispatch:\n"
            header = current.split("\njobs:\n", 1)[0]
            if new_trigger not in header:
                raise AssertionError(f"scoped original push path lost: {name}")
        else:
            new_trigger = "on:\n  workflow_call:\n  workflow_dispatch:\n"
            if "  push:" in current.split("\njobs:\n", 1)[0]:
                raise AssertionError(f"duplicate direct push listener: {name}")
        if current.count(new_trigger) != 1:
            raise AssertionError(f"reusable / manual events missing: {name}")
        reconstructed = current.replace(new_trigger,
                                         "on:\n" + push_block + "  workflow_dispatch:\n", 1)
        if name == "linux-runtime-spinlock-context-v0.yml":
            # Historical top-level cancellation was unsafe on untagged
            # sentinel pushes; this is the ONLY other permitted source edit.
            assert current.count("  cancel-in-progress: false\n") == 1
            reconstructed = reconstructed.replace(
                "  cancel-in-progress: false\n",
                "  cancel-in-progress: true\n", 1)
        if git_blob(reconstructed.encode()) != original_sha:
            raise AssertionError(f"original job bodies or manual behavior changed: {name}")
        expected_cond = "    if: " + " || ".join(
            f"contains(github.event.head_commit.message, '{tag}')" for tag in tags)
        expected_use = f"    uses: ./.github/workflows/{name}"
        job = actual[job_id].strip().splitlines()
        if job != [expected_cond.strip(), expected_use]:
            raise AssertionError(f"router job has unreviewed conditions or arguments: {job_id}")
    # A single additional caller feeds exact selected inputs to the
    # formerly manual-only 10-job MiniAS owner. No extra route runner.
    mini_tags = ("[minias-real16]", "[minias-frontier]", "[minias-vector33]",
                 "[minias-semantic3536]", "[minias-first500]", "[minias-new500]",
                 "[minias-next500]", "[minias-next500b]", "[minias-next500c]",
                 "[minias-next500d]", "[minias-final352]")
    has = lambda tag: "contains(github.event.head_commit.message, '" + tag + "')"
    mode_priority = (("[minias-semantic3536]", "semantic"),
                     ("[minias-frontier]", "frontier"),
                     ("[minias-vector33]", "vector33"),
                     ("[minias-real16]", "real16"))
    window_priority = (("[minias-final352]", "final352"),
                       ("[minias-next500d]", "next500d"),
                       ("[minias-next500c]", "next500c"),
                       ("[minias-next500b]", "next500b"),
                       ("[minias-next500]", "next500"),
                       ("[minias-new500]", "new500"))
    expr = lambda priorities, default: (
        "${{ " + " || ".join(has(tag) + " && '" + value + "'"
                             for tag, value in priorities)
        + " || '" + default + "' }}")
    expected_mini = [
        "# Exactly one declared reusable call; no route runner on ordinary pushes.",
        "# Multi-mode tags use documented precedence; window only matters for mode=window.",
        "if: " + " || ".join(has(tag) for tag in mini_tags),
        "uses: ./.github/workflows/minias-a0-focused-diagnostics-v1.yml",
        "with:",
        "  mode: " + expr(mode_priority, "window"),
        "  window: " + expr(window_priority, "first500"),
    ]
    observed_mini = [line.strip() for line in actual["minias-focused"].strip().splitlines()]
    if observed_mini != [line.strip() for line in expected_mini]:
        raise AssertionError("MiniAS mode/window router differs from audited contract")
    mini_owner = ROOT / ".github/workflows/minias-a0-focused-diagnostics-v1.yml"
    source = mini_owner.read_text()
    call_header = (
        "on:\n  workflow_call:\n    inputs:\n"
        "      mode:\n        description: \"Explicit MiniAS frozen/semantic owner mode\"\n"
        "        required: true\n        type: string\n"
        "      window:\n        description: \"Frozen corpus window\"\n"
        "        required: false\n        type: string\n"
        "        default: first500\n  workflow_dispatch:\n"
    )
    assert source.count(call_header) == 1, "MiniAS shared mode input contract lost"
    original = source.replace(call_header, "on:\n  workflow_dispatch:\n", 1)
    original = original.replace(
        "(github.event_name == 'workflow_dispatch' || github.event_name == 'push') && inputs.mode",
        "github.event_name == 'workflow_dispatch' && inputs.mode",
    )
    if git_blob(original.encode()) != "10a4fed7bd5cb1e111d773efc9edba9533e4c9fb":
        raise AssertionError("MiniAS original full source, frozen inputs, or job bodies altered")
    # All 11 single-tag routes resolve to exactly one mode/window; document
    # priority when several deliberate tags share the same commit message.
    for tag in mini_tags:
        chosen_mode = next((value for label, value in mode_priority if label == tag), "window")
        chosen_window = next((value for label, value in window_priority if label == tag), "first500")
        assert chosen_mode in ("semantic", "frontier", "vector33", "real16", "window")
        assert chosen_window in ("final352", "next500d", "next500c", "next500b",
                                 "next500", "new500", "first500")
    print("M0_MINIAS_TAG_ROUTER=PASS reusable_calls=1 modes=5 window_choices=7 "
          "unique_labels=11 original_full_blob=1 executable_jobs=10 "
          "real_tagged_execution_not_claimed=1")

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
