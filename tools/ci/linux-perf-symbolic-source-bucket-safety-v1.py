#!/usr/bin/env python3
"""Symbolic source-scoped CALL safety qualification (non-production).

Checks:
  * deterministic selector reference model: unique/shared/orphan, ascending ids,
    original expressions and dynamically appended expression tail;
  * genuine compile equivalence on SHA-pinned Linux sources across baseline,
    candidate, forced-bucket-fallback, forced-owner-fallback, ASan+UBSan;
  * positive and negative source diagnostics, without changing production C.

This is a safety gate, NOT a performance benchmark or QEMU certification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess

def model_selftest():
    randomizer=random.Random(0x517C)
    trials=0
    for n in range(1,6):
        for _ in range(1800):
            e=randomizer.randrange(0,50)
            # -1=orphan, n=ambiguous/shared; ids [0,n) canonical.
            owners=[randomizer.randrange(-1,n+1) for _ in range(e)]
            kinds=[randomizer.choice((False,False,False,True)) for _ in range(e)]
            # Original logic filters definite other-source owners, then CALL.
            source=randomizer.randrange(n)
            late=[randomizer.choice((False,True)) for _ in range(randomizer.randrange(0,12))]
            original=[i for i in range(e)
                      if (owners[i] < 0 or owners[i] >= n or owners[i] == source)
                      and kinds[i]]+[e+i for i,k in enumerate(late) if k]
            own=[i for i in range(e) if kinds[i] and owners[i]==source]
            common=[i for i in range(e) if kinds[i] and (owners[i]<0 or owners[i]>=n)]
            # Same ordered merge implementation as the C experiment.
            selected=[]
            i=j=0
            while i<len(own) or j<len(common):
                if j==len(common) or (i<len(own) and own[i]<common[j]):
                    selected.append(own[i]);i+=1
                else:
                    selected.append(common[j]);j+=1
            selected += [e+i for i,k in enumerate(late) if k]
            if selected!=original or len(set(selected))!=len(selected):
                raise RuntimeError("selector ordering/shared/late invariant broken "+repr(
                    (n,owners,kinds,source,late,selected,original)))
            trials+=1
    print("MINIC_SYMBOLIC_BUCKET_MODEL=PASS cases=%d shared_orphan_late=true" % trials)
    return trials

def sha(file):
    return hashlib.sha256(file.read_bytes()).hexdigest() if file.is_file() else None

def compile_one(driver,inp,out,environment=None):
    env=os.environ.copy()
    if environment: env.update(environment)
    p=subprocess.run([str(driver),"-S",str(inp),"-o",str(out)],env=env,
                     stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                     errors="replace",text=True,timeout=180)
    msg=p.stderr[-3500:]
    for bad in ("ERROR: AddressSanitizer","runtime error:","LeakSanitizer:",
                "AddressSanitizer:DEADLYSIGNAL","SUMMARY: UndefinedBehaviorSanitizer"):
        if bad in msg:
            raise RuntimeError("sanitizer failure "+str(inp)+" "+bad+" "+msg)
    return dict(rc=p.returncode,sha=sha(out),stderr=msg)

def main():
    a=argparse.ArgumentParser()
    for name in ("corpus","baseline_driver","candidate_driver","forced_driver","sanitized_driver","out"):
        a.add_argument("--"+name.replace("_","-"),required=True,type=Path)
    v=a.parse_args()
    checks=model_selftest()
    v.out.mkdir(parents=True,exist_ok=True)
    for name in ("baseline_driver","candidate_driver","forced_driver","sanitized_driver"):
        if not getattr(v,name).is_file():raise RuntimeError("missing driver "+name)
    original={}
    for row in (v.corpus/"selected-tus.txt").read_text().splitlines():
        parts=row.split("\t",3)
        if len(parts)!=4:raise RuntimeError("invalid frozen corpus manifest")
        original[parts[1]]=parts[2]
    if len(original)!=500:raise RuntimeError("not authentic first500")
    root=(v.corpus/"kbuild").resolve()
    chosen=("kernel/events/core.o","kernel/bpf/btf.o",
            "fs/ext4/fsync.o","kernel/printk/sysctl.o")
    samples=[]
    for name in chosen:
        p=(root/original[name]).resolve()
        if not p.is_relative_to(root) or not p.is_file():raise RuntimeError("source path invalid")
        samples.append((name,p,True))
    fixtures={
        "simple":("int myglobal; static inline int use_ptr(int *p,int x) { return *p+x; }\nint entry(void){return use_ptr(&myglobal,3);}\n",True),
        "nested":("int a; static inline int *pass(int *x){return x;}\nstatic inline int top(int *x){return *pass(x);}\nint entry(void){return top(&a);}\n",True),
        "invalid":("static inline int foo(int *p){return *p;}\nint bar(void){ return foo(&missing_symbol);}\n",False),
        "empty":("int one(void){ return 1; }\n",True),
    }
    for name,(source,accept) in fixtures.items():
        inp=v.out/(name+".i");inp.write_text(source)
        samples.append(("fixture:"+name,inp,accept))
    envs={
        "baseline":(v.baseline_driver,{}),
        "candidate":(v.candidate_driver,{}),
        "bucket_oom_fallback":(v.forced_driver,{"MINIC_TEST_FORCE_SYMBOLIC_BUCKET_FALLBACK":"1"}),
        "owner_oom_fallback":(v.forced_driver,{"MINIC_TEST_FORCE_SYMBOLIC_OWNER_FALLBACK":"1"}),
        "asan_ubsan":(v.sanitized_driver,{"ASAN_OPTIONS":"detect_leaks=1:halt_on_error=1",
                                         "UBSAN_OPTIONS":"halt_on_error=1:print_stacktrace=1"}),
    }
    rows=[]
    for name,inp,accept in samples:
        tag=name.replace("/","_").replace(":","_").replace(".","_")
        data={}
        for label,(driver,env) in envs.items():
            data[label]=compile_one(driver,inp,v.out/(tag+"."+label+".s"),env)
            yes=data[label]["rc"]==0 and data[label]["sha"] is not None
            if yes != accept:
                raise RuntimeError("unexpected compile acceptance "+name+" "+label+" "+repr(data[label]))
            if label in ("bucket_oom_fallback","owner_oom_fallback"):
                expected=("MINIC_TEST_FORCE_SYMBOLIC_BUCKET_FALLBACK" if label=="bucket_oom_fallback"
                          else "MINIC_TEST_FORCE_SYMBOLIC_OWNER_FALLBACK")
                if expected not in data[label]["stderr"]:
                    raise RuntimeError("fault injection did not exercise intended path "+name+" "+label)
        if accept and len({v["sha"] for v in data.values()})!=1:
            raise RuntimeError("assembly diverged "+name+" "+repr(data))
        if not accept and any(r["sha"] is not None for r in data.values()):
            raise RuntimeError("rejected sample unexpectedly wrote assembly "+name)
        rows.append(dict(owner=name,valid=accept,arm_rc={k:d["rc"] for k,d in data.items()},
                         output_sha=data["baseline"]["sha"]))
        print("MINIC_SYMBOLIC_BUCKET_SAFETY_CASE=PASS owner=%s accepted=%s arms=5"%
              (name,str(accept).lower()),flush=True)
    result={"model_trials":checks,"cases":len(rows),"arms":list(envs),
            "assembly_identical_for_valid":True,"diagnostic_failure_behavior_identical":True,
            "not_2064_qemu_certified":True,"rows":rows}
    (v.out/"safety-summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("MINIC_SYMBOLIC_BUCKET_SAFETY=PASS cases=%d arms=5 model=%d" %
          (len(rows),checks))

if __name__=="__main__":
    main()
