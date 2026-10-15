#!/usr/bin/env python3
"""Ensure both historical early Runtime owners survive as independent jobs."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / ".github/workflows/linux-runtime-fdt-isolation-v1.yml"
ORIGINAL = {
    "linux-runtime-fdt-isolation-v1.yml": (
        "83feffda9150072a849f677c58856f77975968cc",
        ("fdt-isolation", "fdt-ro-codegen"), "linux-runtime-fdt-isolation-v1"),
    "linux-runtime-generated-kallsyms-first-die-v0.yml": (
        "bd8f74957937f1bfbe9adcd30c54042fb6bdf38c",
        ("generated-kallsyms-first-die", "kallsyms-first-fault-abc", "kallsyms-object-diagnose"),
        "linux-runtime-generated-kallsyms-first-die"),
}
def jobs(t):
    section = t.split("\njobs:\n", 1)[1]
    markers = list(re.finditer(r"(?m)^  ([a-z][a-z0-9-]*):\s*$", section))
    return {m.group(1): section[m.start():markers[i+1].start() if i+1<len(markers) else len(section)].strip()
            for i,m in enumerate(markers)}
def clean(s):
    return "\n".join(line for line in s.splitlines()
        if not re.search(r"^    concurrency:$|^      group: linux-runtime-(?:fdt-isolation-v1|generated-kallsyms-first-die)-|^      cancel-in-progress: true$", line)
    ).strip()
def main():
    c=WORK.read_text()
    cur=jobs(c)
    if len(cur)!=5: raise AssertionError("expected five independently reported runtime jobs")
    expected=set()
    for filename,(sha,job_ids,group) in ORIGINAL.items():
        p=ROOT/".github/workflows-disabled"/filename
        actual=subprocess.check_output(["git","hash-object",str(p)],text=True).strip()
        if actual!=sha: raise AssertionError(f"archived workflow byte identity changed: {filename}")
        orig=jobs(p.read_text())
        if set(orig)!=set(job_ids): raise AssertionError(f"archived job set changed: {filename}")
        expected.update(job_ids)
        for name in job_ids:
            if clean(cur[name])!=orig[name]:
                raise AssertionError(f"original executable body altered: {name}")
            grouptext="group: "+group+"-"+"$"+"{{ github.ref }}-"+name
            if grouptext not in cur[name] or "cancel-in-progress: true" not in cur[name]:
                raise AssertionError(f"job-scoped cancellation group missing: {name}")
    if set(cur)!=expected: raise AssertionError("unexpected combined job identities")
    for mode in ("isolation","codegen","generated","three-way","object","all"):
        if "          - '"+mode+"'" not in c:
            raise AssertionError(f"manual mode lost: {mode}")
    if "default: 'isolation'" not in c or '"agent/linux-expanded-kbuild-v0"' not in c:
        raise AssertionError("original default or branch trigger lost")
    if '"tools/ci/linux-early-runtime-link-v2.sh"' not in c:
        raise AssertionError("shared early-link trigger lost")
    for tag in ("[linux-fdt-isolation]","[linux-fdt-codegen]",
                "[linux-kallsyms-generated]","[linux-kallsyms-three-way]","[linux-kallsyms-object]"):
        if tag not in c: raise AssertionError(f"old opt-in tag missing: {tag}")
    print("M0_RUNTIME_FDT_KALLSYMS=PASS canonical_archives=2 independent_jobs=5 cache_oracles_exact=5")
if __name__=="__main__": main()
