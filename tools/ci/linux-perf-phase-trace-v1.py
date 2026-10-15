#!/usr/bin/env python3
"""Phase attribution using preexisting MINIC_BOOTSTRAP_TRACE, frozen .i.

Only approximates phase times from timestamped stderr; NOT a throughput A/B,
new compiler validation, or Linux QEMU certification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import threading
import time

STAGES=("parse","normalize","core-set","core-validate","layout","codegen")
PAT=re.compile(r"MINIC_BOOTSTRAP_TRACE stage=([a-z-]+) state=([a-z-]+) functions=(\d+)")

def digest(p):
    h=hashlib.sha256()
    with p.open("rb") as f:
        for part in iter(lambda:f.read(1048576),b""): h.update(part)
    return h.hexdigest()

def attribute(events):
    begin,elapsed,funcs={},{},{}
    for when,line in events:
        match=PAT.search(line)
        if not match or match.group(1) not in STAGES: continue
        stage,action,count=match.groups()
        if action=="begin":
            if stage in begin: raise RuntimeError("duplicate begin "+stage)
            begin[stage]=when
            funcs[stage]=int(count)
        elif action=="end-ok":
            if stage not in begin or stage in elapsed:
                raise RuntimeError("unmatched end "+stage)
            elapsed[stage]=round((when-begin[stage])/1000000,3)
        elif action=="end-fail":
            raise RuntimeError("compiler trace failed stage "+stage)
    if set(elapsed)!=set(STAGES):
        raise RuntimeError("missing stage trace "+repr(set(STAGES)-set(elapsed)))
    return elapsed,funcs

def compute_interstage_gaps(events):
    """Measure work outside the original six stage spans, without patching MiniC.

    Timestamps come from the same stderr reader. These gaps are NOT attributed
    to a specific pass until the compiler source between markers is examined.
    """
    starts, ends = {}, {}
    for when, line in events:
        match = PAT.search(line)
        if not match or match.group(1) not in STAGES:
            continue
        stage, action, _ = match.groups()
        if action == "begin":
            starts[stage] = when
        elif action == "end-ok":
            ends[stage] = when
    if set(starts) != set(STAGES) or set(ends) != set(STAGES):
        raise RuntimeError("incomplete stage boundaries for interstage attribution")
    gaps = {}
    for a, b in zip(STAGES, STAGES[1:]):
        ns = starts[b] - ends[a]
        if ns < 0:
            raise RuntimeError("overlapping or reordered compiler stages: " + a + " / " + b)
        gaps[a + "-to-" + b] = round(ns / 1000000, 3)
    return gaps


def sample(executable,source,output,limit):
    env=dict(os.environ,MINIC_BOOTSTRAP_TRACE="1")
    begun=time.perf_counter_ns()
    with subprocess.Popen([str(executable),"-S",str(source),"-o",str(output)],
                          env=env,stdout=subprocess.DEVNULL,
                          stderr=subprocess.PIPE,text=True,errors="replace",bufsize=1) as p:
        timer=threading.Timer(limit,p.kill)
        timer.start()
        try:
            events=[(time.perf_counter_ns(),line.rstrip("\n")) for line in p.stderr]
            rc=p.wait()
        finally:
            timer.cancel()
    if rc!=0 or not output.is_file() or output.stat().st_size==0:
        raise RuntimeError("trace compiler failed rc="+str(rc)+" stderr="+repr(events[-3:]))
    stages,functions=attribute(events)
    gaps=compute_interstage_gaps(events)
    return {"wall_ms":round((time.perf_counter_ns()-begun)/1000000,3),
            "stage_ms":stages,"interstage_gap_ms":gaps,
            "function_counts":functions, "assembly_sha":digest(output)}

def main():
    cli=argparse.ArgumentParser()
    cli.add_argument("--self-test",action="store_true")
    cli.add_argument("--input",type=Path)
    cli.add_argument("--baseline",type=Path)
    cli.add_argument("--candidate",type=Path)
    cli.add_argument("--out",type=Path)
    ns=cli.parse_args()
    if ns.self_test:
        events=[]
        for i,stage in enumerate(STAGES):
            events.extend([(i*3000000,"MINIC_BOOTSTRAP_TRACE stage="+stage+" state=begin functions=10"),
                          (i*3000000+2000000,"MINIC_BOOTSTRAP_TRACE stage="+stage+" state=end-ok functions=10")])
        e,f=attribute(events)
        assert len(e)==6 and all(x==2 for x in e.values())
        gaps=compute_interstage_gaps(events)
        assert len(gaps)==5 and all(x==1 for x in gaps.values())
        print("MINIC_PHASE_TRACE_SELFTEST=PASS six_stages=1 five_interstage_gaps=1")
        return
    if not all((ns.input,ns.baseline,ns.candidate,ns.out)):
        cli.error("all inputs required")
    for x in (ns.input,ns.baseline,ns.candidate):
        if not x.is_file(): raise RuntimeError("missing "+str(x))
    ns.out.mkdir(parents=True,exist_ok=True)
    results={}
    for mode,executable in (("before",ns.baseline),("after",ns.candidate)):
        results[mode]=sample(executable.resolve(),ns.input.resolve(),ns.out/(mode+".s"),180)
    if results["before"]["assembly_sha"]!=results["after"]["assembly_sha"]:
        raise RuntimeError("different assembly bytes; cannot attribute performance")
    data={"schema":"minic-existing-stage-trace-v1",
          "input_sha256":digest(ns.input),
          "results":results,
          "meaning":"approximate diagnosis, not paired speedup"}
    (ns.out/"phases.json").write_text(json.dumps(data,indent=2)+"\n")
    print("MINIC_PHASE_TRACE=PASS fixed_input=true assembly_equal=true")
    for stage in STAGES:
        old=results["before"]["stage_ms"][stage]
        new=results["after"]["stage_ms"][stage]
        print(f"MINIC_PHASE_TRACE_STAGE name={stage} old_ms={old:.2f} new_ms={new:.2f} delta_ms={old-new:.2f}")
    for gap in (a+"-to-"+b for a,b in zip(STAGES, STAGES[1:])):
        before=results["before"]["interstage_gap_ms"][gap]
        after=results["after"]["interstage_gap_ms"][gap]
        print(f"MINIC_PHASE_TRACE_GAP name={gap} old_ms={before:.2f} new_ms={after:.2f} delta_ms={before-after:.2f}")
    for mode in ("before","after"):
        item=results[mode]
        measured=sum(item["stage_ms"].values())+sum(item["interstage_gap_ms"].values())
        external=item["wall_ms"]-measured
        if external < -1:
            raise RuntimeError("phase boundaries exceed full compiler invocation")
        print(f"MINIC_PHASE_TRACE_RESIDUAL mode={mode} wall_ms={item['wall_ms']:.2f} "
              f"unclassified_before_parse_or_after_codegen_ms={external:.2f}")

    print("MINIC_PHASE_TRACE_SCOPE=DIAGNOSTIC_ONLY not_QEMU_p1")

if __name__=="__main__":
    main()
