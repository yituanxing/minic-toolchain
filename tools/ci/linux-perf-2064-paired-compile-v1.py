#!/usr/bin/env python3
"""Opt-in actual Kbuild .i -> .s paired wall-time A/B over all 2064 Linux TUs.

Same preprocessed .i for both compiler binaries; balanced order per TU.
Measured numbers exclude GNU preprocessing and GNU assembly.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda: f.read(1<<20), b""):
            h.update(b)
    return h.hexdigest()

def run(driver, source, output, stdout, stderr, owner, arm):
    env = os.environ.copy()
    env.setdefault("CORE_FAST_TRACE", "1")
    start = time.perf_counter_ns()
    try:
        p = subprocess.run([str(driver), "-S", str(source), "-o", str(output)],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=3600, check=False, env=env)
    except subprocess.TimeoutExpired as e:
        raise RuntimeError(f"MiniC A/B timeout owner={owner} arm={arm}") from e
    ms = (time.perf_counter_ns()-start)/1000000
    if arm == "candidate":
        stdout.write_bytes(p.stdout)
        stderr.write_bytes(p.stderr)
    if p.returncode or not output.is_file() or output.stat().st_size == 0:
        raise RuntimeError(f"MiniC A/B failure owner={owner} arm={arm} rc={p.returncode} stderr={p.stderr[-1200:].decode(errors='replace')}")
    return ms, sha256(output)

def pair(a):
    for f in (a.input, a.baseline, a.candidate,
              a.baseline.parent/"minic-cc", a.candidate.parent/"minic-cc"):
        if not f.is_file():
            raise RuntimeError("MiniC full A/B missing file: "+str(f))
    if sha256(a.baseline.parent/"minic-cc")==sha256(a.candidate.parent/"minic-cc"):
        raise RuntimeError("MiniC full A/B compilers identical")
    parity = hashlib.sha256(a.owner.encode()).digest()[0] & 1
    order = ("baseline", "candidate") if parity == 0 else ("candidate", "baseline")
    measured={}
    for arm in order:
        driver = a.baseline if arm=="baseline" else a.candidate
        output = a.baseline_assembly if arm=="baseline" else a.assembly
        measured[arm]=run(driver,a.input,output,a.stdout,a.stderr,a.owner,arm)
    if measured["baseline"][1]!=measured["candidate"][1]:
        raise RuntimeError("MiniC full A/B assembly mismatch owner="+a.owner)
    fields=[a.owner, "%.3f"%measured["baseline"][0], "%.3f"%measured["candidate"][0],
            str(a.input.stat().st_size), measured["candidate"][1],order[0]]
    a.log.parent.mkdir(parents=True,exist_ok=True)
    with a.log.open("a") as out:
        fcntl.flock(out.fileno(),fcntl.LOCK_EX)
        out.write("\t".join(fields)+"\n")
        out.flush()
        fcntl.flock(out.fileno(),fcntl.LOCK_UN)

def summarize(a):
    # Kbuild legitimately invokes the stage2 compiler for helper objects
    # outside our selected 295/294 target list, and occasionally recompiles
    # a target. Historical `COHORT_STAGE_TIME` recorded those events too.
    # Keep all raw evidence, but report exactly one LAST compilation per
    # authenticated requested object (the final candidate .o producer).
    expected=[s.strip() for s in a.objects_file.read_text().splitlines()
              if s.strip() and not s.lstrip().startswith("#")]
    if len(expected)!=len(set(expected)):
        raise RuntimeError("Duplicate owners in selected Kbuild manifest")
    last={}
    raw_lines=0
    for line in a.log.read_text().splitlines():
        f=line.split("\t")
        if len(f)!=6:
            raise RuntimeError("Bad full A/B row")
        owner,old,new,bytes_,digest,first=f
        if first not in ("baseline","candidate") or len(digest)!=64:
            raise RuntimeError("Invalid full A/B provenance")
        old,new=float(old),float(new)
        if min(old,new)<=0:
            raise RuntimeError("Invalid full A/B duration")
        if not all(c in "0123456789abcdef" for c in digest):
            raise RuntimeError("Invalid assembly SHA")
        raw_lines+=1
        if owner in expected:
            last[owner]={"owner":owner,"baseline_ms":old,"candidate_ms":new,
                         "input_bytes":int(bytes_),"asm_sha256":digest,"first":first}
    rows=[last[owner] for owner in expected if owner in last]
    if len(rows)!=len(expected):
        missing=set(expected)-set(last)
        raise RuntimeError(f"Full A/B incomplete expected={len(expected)} actual={len(rows)} missing={sorted(missing)[:6]}")
    old=sum(x["baseline_ms"] for x in rows)
    new=sum(x["candidate_ms"] for x in rows)
    result={"schema":"minic-real-linux-paired-2064-v2","objects":len(rows),
            "raw_kbuild_compiler_invocations":raw_lines,
            "extra_kbuild_compiler_invocations":raw_lines-len(rows),
            "baseline_total_ms":round(old,3),"candidate_total_ms":round(new,3),
            "speedup":round(old/new,5),"reduction_pct":round(100*(1-new/old),3),
            "baseline_first":sum(x["first"]=="baseline" for x in rows),
            "regressions":sum(x["candidate_ms"]>x["baseline_ms"] for x in rows),
            "slowest_candidate":sorted(rows,key=lambda r:r["candidate_ms"],reverse=True)[:15],
            "largest_savings":sorted(rows,key=lambda r:r["baseline_ms"]-r["candidate_ms"],reverse=True)[:15],
            "largest_regressions":sorted(rows,key=lambda r:r["candidate_ms"]-r["baseline_ms"],reverse=True)[:15]}
    a.out.write_text(json.dumps(result,indent=2)+"\n")
    print(f"MINIC_FULL_PERF_AB=PASS objects={len(rows)} raw_invocations={raw_lines} "
          f"baseline_ms={old:.3f} candidate_ms={new:.3f} speedup={old/new:.5f} "
          f"reduction_pct={100*(1-new/old):.3f} baseline_first={result['baseline_first']} "
          f"regressions={result['regressions']}",flush=True)
    for row in result["slowest_candidate"][:5]:
        print(f"MINIC_FULL_PERF_HOT owner={row['owner']} old_ms={row['baseline_ms']:.3f} "
              f"new_ms={row['candidate_ms']:.3f}",flush=True)


def main():
    p=argparse.ArgumentParser()
    subs=p.add_subparsers(dest="action",required=True)
    one=subs.add_parser("pair")
    for n in ("baseline","candidate","input","assembly","baseline-assembly","stdout","stderr","log"):
        one.add_argument("--"+n,type=Path,required=True)
    one.add_argument("--owner",required=True)
    summ=subs.add_parser("summarize")
    for n in ("log","objects-file","out"):
        summ.add_argument("--"+n,type=Path,required=True)
    args=p.parse_args()
    (pair if args.action=="pair" else summarize)(args)

if __name__=="__main__":
    main()
