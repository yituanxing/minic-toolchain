#!/usr/bin/env python3
"""Prove active MiniObjcopy ELF regression/Linux-tool jobs and archive fidelity.

The 2026-09-02 historical Image oracle depended on run 33623125809, now
reporting zero surviving artifacts. Its *entire* previous three-job workflow
is archived under workflows-disabled at its exact Git Blob SHA. Do not quietly
present this unreproducible job as an active Linux certification.
"""
from pathlib import Path
import hashlib
import re

ROOT = Path(__file__).resolve().parents[2]
ACTIVE = ROOT / ".github/workflows/miniobjcopy-strip-regressions-v1.yml"
ARCHIVE = ROOT / ".github/workflows-disabled/miniobjcopy-strip-regressions-legacy-image-2026-10-09.yml"
ARCHIVED_SHA = "851459610d6dd5ca4a528409638a5b840dbd7a46"
ORIGINAL_SHA = "5776fe8d2bd51fb485596274f44e8c9af5da7a5c"
# Reviewed existing three-job extension: a GNU-only MiniObjcopy Linux mode.
# The historical combined MiniLD+MiniObjcopy path stays intact and the
# forbidden expired Image-oracle source remains exclusively archived.
APPROVED_SINGLE_SHA = "b3f1dafca566b3e103ab2b4d5aa0ae5f12be41fa"
OLD_IF = "    if: (github.event_name == 'workflow_dispatch' && (inputs.mode == 'regressions' || inputs.mode == 'all')) || (github.event_name == 'push' && contains(github.event.head_commit.message, '[miniobjcopy-strip-regressions]'))"
NEW_IF = "    if: (github.event_name == 'workflow_dispatch' && (inputs.mode == 'regressions' || inputs.mode == 'all')) || (github.event_name == 'push' && (contains(github.event.head_commit.message, '[miniobjcopy-strip-regressions]') || needs.route.outputs.elf_changed == 'true'))"
MARKER = "\n  # Exact frozen GNU-vs-MiniObjcopy Image oracle; artifact run 33623125809"

def gitsha(raw: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()

def jobs(y: str) -> set[str]:
    return set(re.findall(r"(?m)^  ([a-z][a-z0-9-]*):\s*$",
                          y.split("\njobs:\n", 1)[1]))

def main() -> None:
    old_raw = ARCHIVE.read_bytes()
    if gitsha(old_raw) != ARCHIVED_SHA:
        raise AssertionError("historical MiniObjcopy Image/ELF owner was changed or lost")
    old = old_raw.decode("utf-8")
    active = ACTIVE.read_text()
    if jobs(old) != {"route", "regressions", "linux-tool", "linux-image"}:
        raise AssertionError("historical 4-job source contract incomplete")
    if jobs(active) != {"route", "regressions", "linux-tool"}:
        raise AssertionError("active MiniObjcopy must own exactly 3 runnable jobs")
    if old.count(MARKER) != 1 or old.count("          - linux-image\n") != 1:
        raise AssertionError("frozen Image mode boundary is no longer unambiguous")
    if old.count("  cancel-in-progress: true\n") != 1:
        raise AssertionError("original MiniObjcopy concurrency proof drift")
    expected = old.replace("  cancel-in-progress: true\n",
                           "  cancel-in-progress: false\n", 1)
    expected = expected.replace("          - linux-image\n", "", 1)
    expected = expected.split(MARKER, 1)[0].rstrip() + "\n"
    if active != expected:
        if gitsha(active.encode()) != APPROVED_SINGLE_SHA:
            raise AssertionError("active MiniObjcopy integration changed outside approved GNU-only single mode")
        for token in (
            "- linux-single",
            "[miniobjcopy-linux-single]",
            "MINIOBJCOPY_SINGLE_MODE",
            "LD=riscv64-linux-gnu-ld OBJCOPY=",
            "test ! -s \"$work/minild.trace\"",
            "MINIOBJCOPY_LINUX_SINGLE=PASS",
            "MINIOBJCOPY_LINUX_SINGLE_RUNTIME=PASS",
            "LINUX_RUNTIME_PROFILE=fast",
            "- linux-replay",
            "[miniobjcopy-linux-replay]",
            "MINIOBJCOPY_REPLAY_MODE",
            "MINILD_FINAL_IMAGE_REPLAY=PASS",
            "MINILD_FINAL_MINIOBJCOPY_LINUX_RUNTIME=PASS",
            "linux-native-triple-replay",
            "[miniobjcopy-native-triple-replay]",
            "MINIOBJCOPY_TRIPLE_REPLAY_MODE",
            "38184298699",
            "6b8ec7591c1e8270b4e1bdb301fccbec0352447c9c895df7e07156eed6a674d1",
            "MINIAS_MINIAR_MINIOBJCOPY_IMAGE_REPLAY=PASS",
            "MINIAS_MINIAR_MINIOBJCOPY_LINUX_RUNTIME=PASS",
            "run-id: '38160503540'",
            "db7bb695bb9dcd5985a726e0ea3b5d54b32f01c195b6c91ee117bd146d999693",
        ):
            if token not in active:
                raise AssertionError(f"approved MiniObjcopy single-tool gate missing: {token}")
    if "run-id: 33623125809" in active or "[miniobjcopy-linux-image]" in active:
        raise AssertionError("expired historical artifact reactivated as a live oracle")

    route = re.search(r"(?ms)^  route:\n.*?(?=^  regressions:\n)", old)
    if route is None:
        raise AssertionError("historical ELF route missing")
    original = old[:route.start()] + old[route.end():]
    original = original.replace("  regressions:\n    needs: route\n" + NEW_IF,
                                "  regressions:\n" + OLD_IF, 1)
    if gitsha(original.encode()) != ORIGINAL_SHA:
        raise AssertionError("pre-router MiniObjcopy original SHA mismatch")
    print("M0_MINIOBJCOPY_ELF_ROUTING=PASS active_jobs=3 "
          "preserved_regression_and_linux_tool=1 "
          "legacy_image_archived=1 original_sources=2 "
          "noncancelling_workflow=1 expired_run_excluded=1 approved_gnu_only_single_mode=1")

if __name__ == "__main__":
    main()
