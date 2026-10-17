#!/usr/bin/env python3
"""Read-only actual current-production MiniC resource census on frozen GNU Kbuild first500.

Measures 500/500 distinct MiniC -S invocations with max four processes:
wall, user/sys CPU and single-process peak RSS from /usr/bin/time.
Never claims the sum of per-TU wall is elapsed four-worker build time or that
a per-process peak is the full runner's concurrent memory peak.
Requires original SHA-locked first500 corpus and an exact compiled release binary.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import tempfile
import time

def digest(path):
    h=hashlib.sha256()
    with path.open("rb") as fd:
        for chunk in iter(lambda:fd.read(1<<20),b""):
            h.update(chunk)
    return h.hexdigest()

def selected_inputs(folder):
    manifest=folder/"selected-tus.txt"
    rows=[]
    seen=set()
    base=(folder/"kbuild").resolve()
    for line in manifest.read_text().splitlines():
        parts=line.split("\t",3)
        if len(parts)!=4:
            raise RuntimeError("bad selected TU row")
        index,obj,rel_i,_=parts
        number=int(index)
        if number in seen:
            raise RuntimeError("repeated index")
        seen.add(number)
        path=(base/rel_i).resolve()
        if not path.is_relative_to(base) or not path.is_file() or path.stat().st_size==0:
            raise RuntimeError("bad frozen source path "+str(path))
        rows.append((number,obj,rel_i,path))
    if len(rows)!=500 or len(seen)!=500:
        raise RuntimeError("expected exactly 500 frozen inputs")
    return rows

def one_tu(info,driver):
    idx,obj,relative,source=info
    with tempfile.TemporaryDirectory(prefix="minic-current-resource-") as td:
        root=Path(td)
        target=root/"result.s"
        timer=root/"time.txt"
        command=["/usr/bin/time","-f","WALL_SECONDS=%e\nUSER_SECONDS=%U\nSYS_SECONDS=%S\nMAX_RSS_KIB=%M",
                 "-o",str(timer),str(driver),"-S",str(source),"-o",str(target)]
        start=time.perf_counter_ns()
        p=subprocess.run(command,capture_output=True,errors="replace",text=True,timeout=180)
        outer_ms=(time.perf_counter_ns()-start)/1e6
        if p.returncode!=0 or not target.is_file() or target.stat().st_size==0:
            raise RuntimeError("MiniC failed "+obj+" rc="+str(p.returncode)+" "+p.stderr[-1400:])
        metrics={}
        for line in timer.read_text().splitlines():
            k,_,value=line.partition("=")
            if k in ("WALL_SECONDS","USER_SECONDS","SYS_SECONDS","MAX_RSS_KIB"):
                metrics[k]=float(value) if k!="MAX_RSS_KIB" else int(value)
        if len(metrics)!=4 or metrics["MAX_RSS_KIB"]<=0:
            raise RuntimeError("incomplete actual per-TU /usr/bin/time measurement "+obj)
        return {"index":idx,"object":obj,"input":relative,
                "input_bytes":source.stat().st_size,
                "assembly_sha256":digest(target),"assembly_bytes":target.stat().st_size,
                "wall_ms":round(outer_ms,3),
                "time_wall_ms":round(metrics["WALL_SECONDS"]*1000,3),
                "user_ms":round(metrics["USER_SECONDS"]*1000,3),
                "sys_ms":round(metrics["SYS_SECONDS"]*1000,3),
                "peak_rss_kib":metrics["MAX_RSS_KIB"]}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--corpus",type=Path,required=True)
    p.add_argument("--driver",type=Path,required=True)
    p.add_argument("--out",type=Path,required=True)
    p.add_argument("--jobs",type=int,choices=(1,2,4),default=4)
    a=p.parse_args()
    corpus=a.corpus.resolve()
    binary=a.driver.resolve()
    if not binary.is_file() or not (binary.parent/"minic-cc").is_file():
        raise RuntimeError("missing production compiler and backend")
    selected=selected_inputs(corpus)
    start=time.perf_counter()
    rows=[]
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        pending=[pool.submit(one_tu,x,binary) for x in selected]
        for i,future in enumerate(as_completed(pending),1):
            rows.append(future.result())
            if i%50==0:
                print("MINIC_CURRENT_RESOURCE500_PROGRESS=%d/500"%i,flush=True)
    elapsed_ms=(time.perf_counter()-start)*1000
    rows.sort(key=lambda v:v["index"])
    rss=[v["peak_rss_kib"] for v in rows]
    walls=[v["wall_ms"] for v in rows]
    cpus=[v["user_ms"]+v["sys_ms"] for v in rows]
    def percentile(vals,p):
        data=sorted(vals)
        return data[max(0,min(len(data)-1,(len(data)*p+99)//100-1))]
    top=lambda name:[{"object":x["object"],name:x[name]} for x in
                     sorted(rows,key=lambda x:x[name],reverse=True)[:15]]
    summ={"scope":"current production GNU-preprocessed Linux first500 .i -> MiniC -S",
          "inputs_sha256":digest(corpus/"inputs.sha256"),
          "compiler_sha256":digest(binary.parent/"minic-cc"),
          "driver_sha256":digest(binary),
          "jobs":a.jobs,"input_count":len(rows),
          "sum_tu_wall_ms":round(sum(walls),3),
          "sum_tu_user_sys_ms":round(sum(cpus),3),
          "elapsed_parallel_wall_ms":round(elapsed_ms,3),
          "median_tu_wall_ms":round(statistics.median(walls),3),
          "p95_tu_wall_ms":round(percentile(walls,95),3),
          "peak_single_tu_rss_kib":max(rss),
          "median_single_tu_rss_kib":statistics.median(rss),
          "p95_single_tu_rss_kib":percentile(rss,95),
          "top_memory":top("peak_rss_kib"),
          "top_wall":top("wall_ms"),
          "no_old_new_causal_speedup_in_this_test":True,
          "not_full_2064_not_qemu_not_concurrent_rss":True}
    a.out.mkdir(parents=True,exist_ok=True)
    (a.out/"rows.json").write_text(json.dumps(rows,indent=2)+"\n")
    (a.out/"summary.json").write_text(json.dumps(summ,indent=2)+"\n")
    print("MINIC_CURRENT_RESOURCE500=PASS owners=500 jobs=%d "
          "tu_wall_sum_ms=%.3f user_sys_sum_ms=%.3f four_worker_elapsed_ms=%.3f "
          "median_peak_kib=%.0f max_peak_kib=%d p95_peak_kib=%d compiler_sha256=%s" %
          (a.jobs,summ["sum_tu_wall_ms"],summ["sum_tu_user_sys_ms"],
           summ["elapsed_parallel_wall_ms"],summ["median_single_tu_rss_kib"],
           summ["peak_single_tu_rss_kib"],summ["p95_single_tu_rss_kib"],
           summ["compiler_sha256"]),flush=True)

if __name__=="__main__":
    main()
