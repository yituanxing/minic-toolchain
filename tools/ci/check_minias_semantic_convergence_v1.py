#!/usr/bin/env python3
"""Guard MiniAS 3536 semantic oracle consolidation without weakening existing jobs.

Two independent full Git blob archives are the golden contract. The existing
6 focused/window jobs must remain byte-identical. The four semantic jobs keep
everything from their first executable job property onward; only their
job-level dispatch predicates are replaced with a dedicated semantic mode.
"""
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / ".github/workflows/minias-a0-focused-diagnostics-v1.yml"
ARCH = ROOT / ".github/workflows-disabled"
FOCUSED_SHA = "93330555c3913fb78ac1c67d180f2a70dabe951f"
SEMANTIC_SHA = {
    "agent/linux-expanded-kbuild-v0": "519b4b6fa9f3dd6e344761a7e7f6cbbd1de76831",
    "agent/linux-perf-boolean-domain-v1": "96bb3bf93fac928613f944b0042ddbac1c4c0280",
}
FOCUSED = ("real16", "frontier", "vector33", "resolve", "window-shard", "window")
SEMANTIC = ("c-shards", "c149", "native35", "aggregate")

def sha(path):
    return subprocess.check_output(["git", "hash-object", str(path)], text=True).strip()

def jobs(source):
    segment = source.split("\njobs:\n", 1)[1]
    markers = list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$", segment))
    return {
        m.group(1): segment[m.start():markers[i+1].start() if i+1<len(markers) else len(segment)].strip()
        for i,m in enumerate(markers)
    }

def executable(body):
    """Ignore only the job ID and its initial if: predicate, including folded if."""
    m = re.search(r"(?m)^    (?:strategy|runs-on|needs):", body)
    if m is None:
        raise AssertionError("job has no nonconditional executable property")
    return body[m.start():].strip()

def main():
    branch = os.environ.get("GITHUB_REF_NAME", "agent/linux-expanded-kbuild-v0")
    if branch not in SEMANTIC_SHA:
        raise AssertionError(f"unexpected branch {branch}")
    old_focus = ARCH / "minias-a0-focused-diagnostics-v1.yml"
    old_semantics = ARCH / "minias-semantic-oracle3536.yml"
    if sha(old_focus) != FOCUSED_SHA or sha(old_semantics) != SEMANTIC_SHA[branch]:
        raise AssertionError("historical MiniAS source Git blob changed")
    current_text = WORK.read_text()
    old_f = jobs(old_focus.read_text())
    old_s = jobs(old_semantics.read_text())
    cur = jobs(current_text)
    if set(old_f) != set(FOCUSED) or set(old_s) != set(SEMANTIC):
        raise AssertionError("old job identities do not match frozen list")
    if set(cur) != set(FOCUSED + SEMANTIC):
        raise AssertionError("lost semantic or focused independent jobs")
    for name in FOCUSED:
        if cur[name] != old_f[name]:
            raise AssertionError(f"modified preexisting MiniAS focused job: {name}")
    for name in SEMANTIC:
        if executable(cur[name]) != executable(old_s[name]):
            raise AssertionError(f"modified original 3536 executable job: {name}")
        want = ("    if: always() && github.event_name == 'workflow_dispatch' && inputs.mode == 'semantic'"
                if name == "aggregate" else
                "    if: github.event_name == 'workflow_dispatch' && inputs.mode == 'semantic'")
        if want not in cur[name]:
            raise AssertionError(f"changed semantic manual routing: {name}")
    if "    needs: [c-shards, c149, native35]" not in cur["aggregate"]:
        raise AssertionError("semantic aggregation dependencies changed")
    if "          - semantic" not in current_text or "          - window" not in current_text:
        raise AssertionError("old window / new semantic modes not both available")
    if '  C149_EXPORT_RUN: "33248985705"' not in current_text:
        raise AssertionError("original semantic C149 artifact producer lost")
    for token in ("FROZEN_CORPUS_EXPORT_RUN", "FROZEN_SIDECARS_RUN", "FROZEN_CONFIG_GZ_SHA256"):
        if token not in current_text:
            raise AssertionError(f"missing frozen corpus pin {token}")
    for token in ("  actions: read", "  contents: read"):
        if token not in current_text:
            raise AssertionError(f"missing GitHub Action permission {token}")
    print(f"M0_MINIAS_SEMANTIC_CONVERGENCE=PASS branch={branch} jobs=10 original_focused=6 original_semantic=4 original_blob_shas=2")

if __name__ == "__main__":
    main()
