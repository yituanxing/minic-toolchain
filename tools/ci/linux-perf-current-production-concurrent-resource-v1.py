#!/usr/bin/env python3
"""Same-runner 4-vs-7 concurrent CURRENT-producer Linux first500 resource gate.

Reuses existing SHA-frozen corpus selection and /usr/bin/time per-TU recorder.
Samples actual processes' resident bytes and job cgroup memory at runtime.
No new workflow/job, no production source edits, no 2064 runtime claim.

Important: /proc observed summed process RSS double counts shared pages and
misses peaks between polls; cgroup memory.current includes page cache/runner
overhead. Neither represents total system DRAM or seven isolated CI runners.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import importlib.util
import json
import os
from pathlib import Path
import statistics
import threading
import time

SRC=Path(__file__).with_name("linux-perf-current-production-resource500-v1.py")
spec=importlib.util.spec_from_file_location("minic_existing_resource500",SRC)
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

def read_int(path):
    try:
        return int(Path(path).read_text().strip())
    except (ValueError,OSError):
        return None

def read_anon():
    try:
        for line in Path("/sys/fs/cgroup/memory.stat").read_text().splitlines():
            if line.startswith("anon "):
                return int(line.split()[1])
    except OSError:
        pass
    return None

def get_live_compiler_rss(executables):
    # The 'minic' process is a driver; minic-cc child holds the real AST.
    # Identify full executable path, not just a shared binary name.
    total=0
    count=0
    cc_count=0
    try:
        pids=list(Path("/proc").iterdir())
    except OSError:
        return (0,0,0)
    for child in pids:
        if not child.name.isdecimal():
            continue
        try:
            name=(child/"comm").read_text().strip()
            if name not in ("minic","minic-cc"):
                continue
            exe=os.readlink(child/"exe")
            if exe not in executables:
                continue
            parts=(child/"statm").read_text().split()
            if len(parts)<2:
                continue
            rss=int(parts[1])*os.sysconf("SC_PAGE_SIZE")
            total+=rss
            count+=1
            cc_count+=(name=="minic-cc")
        except (OSError,ValueError):
            continue
    return total,count,cc_count

class Sampler:
    def __init__(self,binary,interval=0.08):
        self.execs={str(binary),str(binary.parent/"minic-cc")}
        self.interval=interval
        self.stop=threading.Event()
        self.samples=0
        self.peak_rss=0
        self.peak_pid_count=0
        self.peak_cc_count=0
        self.peak_cgroup=0
        self.peak_cgroup_anon=0
        self.cgroup_baseline=read_int("/sys/fs/cgroup/memory.current")
        self.anon_baseline=read_anon()
        self.thread=threading.Thread(target=self._run,daemon=True)

    def _run(self):
        while not self.stop.is_set():
            rss,pid_count,cc_count=get_live_compiler_rss(self.execs)
            cg=read_int("/sys/fs/cgroup/memory.current")
            anon=read_anon()
            self.peak_rss=max(self.peak_rss,rss)
            self.peak_pid_count=max(self.peak_pid_count,pid_count)
            self.peak_cc_count=max(self.peak_cc_count,cc_count)
            if cg is not None:self.peak_cgroup=max(self.peak_cgroup,cg)
            if anon is not None:self.peak_cgroup_anon=max(self.peak_cgroup_anon,anon)
            self.samples+=1
            self.stop.wait(self.interval)

    def start(self): self.thread.start()

    def finish(self):
        self.stop.set()
        self.thread.join(timeout=2)
        return {
            "observations":self.samples,
            "poll_interval_ms":self.interval*1000,
            "observed_peak_sum_process_rss_kib":self.peak_rss//1024,
            "observed_peak_pid_count":self.peak_pid_count,
            "observed_peak_cc_count":self.peak_cc_count,
            "cgroup_baseline_bytes":self.cgroup_baseline,
            "observed_peak_cgroup_bytes":self.peak_cgroup or None,
            "cgroup_anon_baseline_bytes":self.anon_baseline,
            "observed_peak_cgroup_anon_bytes":self.peak_cgroup_anon or None,
            "cgroup_peak_above_start_kib":(
                (self.peak_cgroup-self.cgroup_baseline)//1024
                if self.cgroup_baseline is not None and self.peak_cgroup else None),
            "cgroup_anon_peak_above_start_kib":(
                (self.peak_cgroup_anon-self.anon_baseline)//1024
                if (self.anon_baseline is not None and
                    self.peak_cgroup_anon >= self.anon_baseline) else None)
        }

def main():
    parser=argparse.ArgumentParser()
    for arg in ("corpus","driver","out"):
        parser.add_argument("--"+arg,required=True,type=Path)
    parser.add_argument("--workers",default="4,7")
    args=parser.parse_args()
    workers=[int(k.strip()) for k in args.workers.split(",")]
    if sorted(workers)!=[4,7]:
        raise ValueError("this qualification compares four and seven worker arms exactly once each")
    worker_order=tuple(workers)  # Keep exact arm order: loop variable below shadows workers.
    corpus=args.corpus.resolve()
    driver=args.driver.resolve()
    if not driver.is_file() or not (driver.parent/"minic-cc").is_file():
        raise RuntimeError("missing MiniC executable pair")
    files=mod.selected_inputs(corpus)
    args.out.mkdir(parents=True,exist_ok=True)
    groups=[]
    for workers in workers:
        sampler=Sampler(driver)
        results=[]
        start=time.perf_counter()
        sampler.start()
        try:
            with ThreadPoolExecutor(max_workers=workers) as pool:
                futures=[pool.submit(mod.one_tu,row,driver) for row in files]
                for index,task in enumerate(as_completed(futures),1):
                    results.append(task.result())
                    if index%100==0:
                        print("MINIC_CURRENT_CONCURRENCY_PROGRESS workers=%d checked=%d/500"%
                              (workers,index),flush=True)
        finally:
            observation=sampler.finish()
        seconds=time.perf_counter()-start
        results.sort(key=lambda row:row["index"])
        if len(results)!=500:
            raise RuntimeError("not all 500 completed for "+str(workers))
        summaries={
            "workers":workers,
            "elapsed_wall_ms":round(seconds*1000,3),
            "sum_tu_wall_ms":round(sum(r["wall_ms"] for r in results),3),
            "sum_tu_user_sys_ms":round(sum(r["user_ms"]+r["sys_ms"] for r in results),3),
            "single_tu_rss_max_kib":max(r["peak_rss_kib"] for r in results),
            "single_tu_rss_median_kib":statistics.median(r["peak_rss_kib"] for r in results),
            "observation":observation,
            "rows":results
        }
        (args.out/("workers%d.json"%workers)).write_text(json.dumps(summaries,indent=2)+"\n")
        groups.append(summaries)
        print("MINIC_CURRENT_CONCURRENCY_ARM=PASS workers=%d elapsed_ms=%.3f "
              "peak_observed_procs_rss_kib=%d cgroup_peak_kib=%s "
              "cgroup_anon_above_start_kib=%s"%
              (workers,summaries["elapsed_wall_ms"],
               observation["observed_peak_sum_process_rss_kib"],
               str(observation["observed_peak_cgroup_bytes"]//1024)
                 if observation["observed_peak_cgroup_bytes"] is not None else "unavailable",
               str(observation["cgroup_anon_peak_above_start_kib"])),flush=True)
    by_workers={group["workers"]:group for group in groups}
    left,right=by_workers[4],by_workers[7]
    matches=sum(a["assembly_sha256"]==b["assembly_sha256"]
                for a,b in zip(left["rows"],right["rows"]))
    if matches!=500:
        raise RuntimeError("same production compiler generated different output in 4-vs-7")
    summary={
        "source":"latest production only, SHA-pinned first500 Linux .i -> MiniC -S",
        "fixed_software_sha256":mod.digest(driver.parent/"minic-cc"),
        "fixed_corpus_sha256":mod.digest(corpus/"inputs.sha256"),
        "run_order":("%d_then_%d; sequential, not interleaved; scheduling/caches can influence timing"%
                     worker_order),
        "same_assembly":matches,
        "elapsed_4_ms":left["elapsed_wall_ms"],
        "elapsed_7_ms":right["elapsed_wall_ms"],
        "speedup_7_over_4":round(left["elapsed_wall_ms"]/right["elapsed_wall_ms"],5),
        "efficiency_note":"two sequential one-pass throughput measurements; no statistical repeat",
        "four":{k:v for k,v in left.items() if k!="rows"},
        "seven":{k:v for k,v in right.items() if k!="rows"},
        "not_seven_separate_CI_runners":True,
        "not_full_2064_qemu":True,
        "memory_note":"sum-process RSS includes shared pages and polling misses spikes; cgroup includes cache"}
    (args.out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("MINIC_CURRENT_CONCURRENCY=PASS checked=500/500 assembly_equal=500/500 "
          "parallel_4_ms=%.3f parallel_7_ms=%.3f seven_over_four=%.5fx "
          "rss4_kib=%d rss7_kib=%d"%
          (left["elapsed_wall_ms"],right["elapsed_wall_ms"],
           summary["speedup_7_over_4"],
           left["observation"]["observed_peak_sum_process_rss_kib"],
           right["observation"]["observed_peak_sum_process_rss_kib"]),flush=True)

if __name__=="__main__":
    main()
