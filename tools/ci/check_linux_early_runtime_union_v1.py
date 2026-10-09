#!/usr/bin/env python3
"""Prove early Linux PI/P1/trace and compiler/QEMU contract workflows converged.

The two earlier canonical YAMLs are retained byte-for-byte, including each
development branch's different Entry Trace code. Every runnable job body must
match the corresponding archived canonical source exactly.
"""
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
CURRENT = ROOT / ".github/workflows/linux-expanded-pi-p1-runtime-v1.yml"
HISTORICAL = {
    "linux-expanded-pi-p1-runtime-v1.yml": {
        "agent/linux-expanded-kbuild-v0": "acd84a8de23ebf908f75eb863df285cfb83b79c0",
        "agent/linux-perf-boolean-domain-v1": "0dff72daea890b37972c64a6411caa18b00cf528",
    },
    "linux-runtime-contract-oracles-v1.yml": {
        "agent/linux-expanded-kbuild-v0": "050e4a8cc965cfca63a736a2cb491e1f038bb9eb",
        "agent/linux-perf-boolean-domain-v1": "050e4a8cc965cfca63a736a2cb491e1f038bb9eb",
    },
}
JOB_SETS = {
    "linux-expanded-pi-p1-runtime-v1.yml": {"pi-runtime", "p1", "trace"},
    "linux-runtime-contract-oracles-v1.yml": {
        "pi-local-symbol", "satp-micro", "qemu-watch-cert", "inconclusive-cert"
    },
}
MODES = {"p1", "pi", "trace", "semantics", "watch", "all"}

def jobs(contents):
    tail = contents.split("\njobs:\n", 1)[1]
    matches = list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$", tail))
    return {
        m.group(1): tail[m.start():matches[i + 1].start() if i + 1 < len(matches) else len(tail)].strip()
        for i, m in enumerate(matches)
    }

def main():
    branch = os.environ.get("GITHUB_REF_NAME", "agent/linux-expanded-kbuild-v0")
    if branch not in HISTORICAL["linux-expanded-pi-p1-runtime-v1.yml"]:
        raise AssertionError(f"unsupported development branch {branch}")
    text = CURRENT.read_text()
    active = jobs(text)
    expected_jobs = set().union(*JOB_SETS.values())
    if set(active) != expected_jobs:
        raise AssertionError(f"lost or unexpected early Runtime jobs: {set(active) ^ expected_jobs}")
    for old_name, sha_by_branch in HISTORICAL.items():
        archived = ROOT / ".github/workflows-disabled" / old_name
        sha = subprocess.check_output(["git", "hash-object", str(archived)], text=True).strip()
        if sha != sha_by_branch[branch]:
            raise AssertionError(f"original canonical YAML changed: {old_name}")
        prior = jobs(archived.read_text())
        if set(prior) != JOB_SETS[old_name]:
            raise AssertionError(f"original canonical job set changed: {old_name}")
        for name, body in prior.items():
            # A deliberate routing repair moves the old Runtime-only branch guard
            # inside the push arm; on manual Performance dispatch the semantic
            # mode must no longer be silently skipped. All other job text stays
            # byte-identical and the exact new if expressions are separately gated.
            if name in {"pi-local-symbol", "satp-micro", "qemu-watch-cert", "inconclusive-cert"}:
                without_if = lambda s: "\n".join(x for x in s.splitlines() if not x.startswith("    if: "))
                if without_if(active[name]) != without_if(body):
                    raise AssertionError(f"historical executable job body changed: {name}")
            elif active[name] != body:
                raise AssertionError(f"original executable job/condition changed: {name}")
    for mode in MODES:
        if "          - " + mode not in text:
            raise AssertionError(f"manual mode lost: {mode}")
    if "default: p1" not in text or '"agent/linux-expanded-kbuild-v0"' not in text:
        raise AssertionError("original default/Runtime push branch lost")
    print(f"M0_EARLY_RUNTIME_UNION=PASS branch={branch} archived_canons=2 independent_jobs=7 exact_job_bodies=7")

if __name__ == "__main__":
    main()
