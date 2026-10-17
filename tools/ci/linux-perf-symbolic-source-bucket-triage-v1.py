#!/usr/bin/env python3
"""Recheck actual 500-corpus outliers using EXISTING paired MiniC A/B runner.

Only one frozen TU is benchmarked at a time, with warmups, 4 alternating pairs,
byte-identical assembly and wall/user/sys P50 + MAD. No new performance runner.
The initial 500-worker timings are noisy; this is a stop/go regression gate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys

REGRESSIONS=(
    "fs/nfs/client.o",
    "arch/riscv/kernel/jump_label.o",
    "arch/riscv/kvm/vcpu_exit.o",
    "kernel/time/vsyscall.o",
    "kernel/utsname_sysctl.o",
    "fs/sysfs/dir.o",
    "fs/nfs/blocklayout/extent_tree.o",
    "arch/riscv/mm/extable.o",
)
CONTROLS=(
    "kernel/events/core.o",
    "mm/memcontrol.o",
    "kernel/printk/sysctl.o",
)

def median_and_mad(nums):
    m=statistics.median(nums)
    return m,statistics.median(abs(v-m) for v in nums)

def main():
    a=argparse.ArgumentParser()
    for name in ("corpus","baseline_driver","candidate_driver","out"):
        a.add_argument("--"+name.replace("_","-"),type=Path,required=True)
    ns=a.parse_args()
    for path in (ns.corpus,ns.baseline_driver,ns.candidate_driver):
        if not path.exists():raise RuntimeError("missing trusted artifact "+str(path))
    selected={}
    for line in (ns.corpus/"selected-tus.txt").read_text().splitlines():
        cols=line.split("\t",3)
        if len(cols)!=4:raise RuntimeError("bad source manifest")
        selected[cols[1]]=cols[2]
    if len(selected)!=500:raise RuntimeError("expected exact 500 selected inputs")
    root=(ns.corpus/"kbuild").resolve()
    ns.out.mkdir(parents=True,exist_ok=True)
    rows=[]
    for owner in REGRESSIONS+CONTROLS:
        if owner not in selected:raise RuntimeError("missing frozen owner "+owner)
        source=(root/selected[owner]).resolve()
        if not source.is_relative_to(root) or not source.is_file():
            raise RuntimeError("not authentic frozen input: "+str(source))
        key=owner.replace("/","_").removesuffix(".o")
        target=ns.out/key
        p=subprocess.run(
            [sys.executable,"tools/ci/linux-perf-pinned-single-paired-ab-v1.py",
             "--input",str(source),"--baseline-driver",str(ns.baseline_driver),
             "--candidate-driver",str(ns.candidate_driver),
             "--repeats","4","--out",str(target)],
            capture_output=True,text=True,errors="replace",timeout=180)
        (ns.out/(key+"-verdict.txt")).write_text(p.stdout+"\n"+p.stderr)
        if p.returncode or "MINIC_ONE_OWNER_AB=PASS" not in p.stdout:
            raise RuntimeError("AB failed owner="+owner+" "+(p.stdout+p.stderr)[-900:])
        data=json.loads((target/"paired-result.json").read_text())
        if not data["assembly_identical"] or data["repeat_per_arm"]!=4:
            raise RuntimeError("A/B mismatched "+owner)
        scores={}
        for arm in ("before","after"):
            samples=[r for r in data["records"] if r["mode"]==arm]
            wall=median_and_mad([r["wall_ms"] for r in samples])
            cpu=median_and_mad([r["user_ms"]+r["sys_ms"] for r in samples])
            scores[arm]={"wall_p50_ms":round(wall[0],3),"wall_mad_ms":round(wall[1],3),
                         "cpu_p50_ms":round(cpu[0],3),"cpu_mad_ms":round(cpu[1],3)}
        b,n=scores["before"],scores["after"]
        wall_diff=n["wall_p50_ms"]-b["wall_p50_ms"]
        cpu_diff=n["cpu_p50_ms"]-b["cpu_p50_ms"]
        wall_threshold=max(5.,3*(b["wall_mad_ms"]+n["wall_mad_ms"]),.03*b["wall_p50_ms"])
        cpu_threshold=max(5.,3*(b["cpu_mad_ms"]+n["cpu_mad_ms"]),.03*b["cpu_p50_ms"])
        confirmed_regression=wall_diff>wall_threshold and cpu_diff>cpu_threshold
        rows.append({"owner":owner,"initial_500_tag":"possible_regression" if owner in REGRESSIONS else "control",
                     **scores,"speedup":round(b["wall_p50_ms"]/n["wall_p50_ms"],5),
                     "wall_diff_ms":round(wall_diff,3),"cpu_diff_ms":round(cpu_diff,3),
                     "wall_threshold_ms":round(wall_threshold,3),
                     "cpu_threshold_ms":round(cpu_threshold,3),
                     "confirmed_regression":confirmed_regression})
        print("MINIC_SYMBOLIC_TRIAGE owner="+owner+" speedup=%.5f cpu_diff_ms=%.3f "
              "wall_diff_ms=%.3f confirmed_regression=%s" %
              (rows[-1]["speedup"],cpu_diff,wall_diff,
               str(confirmed_regression).lower()),flush=True)
    bad=[r["owner"] for r in rows if r["confirmed_regression"]]
    result={"schema":"minic-source-scoped-symbolic-regression-triage-v1",
            "scope":"8 worst single-run 500 regressions + 3 controls, rechecked",
            "not_full_2064_or_qemu":True,"cases":len(rows),
            "consistent_assembly":True,"confirmed_regressions":bad,"rows":rows,
            "decision":"NEEDS_CAUSAL_REVIEW" if bad else "NO_CONFIRMED_REGRESSIONS_IN_SELECTED_OUTLIERS"}
    (ns.out/"triage-summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("MINIC_SYMBOLIC_REGRESSION_TRIAGE=PASS cases=%d confirmed=%d decision=%s" %
          (len(rows),len(bad),result["decision"]))

if __name__=="__main__":
    main()
