#!/usr/bin/env python3
"""Benchmark one REAL frozen Linux .i on unchanged and optimized MiniC drivers.

Only same-input, same-ABI paired timing and byte-identical assembly constitute
PASS. The caller is responsible for collecting the exact Kbuild-produced .i.
"""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import time

def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as fd:
        for data in iter(lambda: fd.read(1 << 20), b""):
            h.update(data)
    return h.hexdigest()

def one(driver, source, asm):
    start = time.perf_counter_ns()
    p = subprocess.run([str(driver), "-S", str(source), "-o", str(asm)],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=120, text=True, errors="replace", check=False)
    ms = (time.perf_counter_ns()-start) / 1e6
    if p.returncode or not asm.is_file() or asm.stat().st_size == 0:
        raise RuntimeError("MiniC compile failed: driver=" + str(driver) +
                           " rc=" + str(p.returncode) + " stderr=" + p.stderr[-900:])
    return {"wall_ms":round(ms, 3), "sha256":sha(asm),
            "assembly_bytes":asm.stat().st_size}

def main():
    p = argparse.ArgumentParser()
    for name in ("input", "baseline_driver", "candidate_driver", "out"):
        p.add_argument("--"+name.replace("_","-"), type=Path, required=True)
    args = p.parse_args()
    inp = args.input.resolve()
    drivers = {"before":args.baseline_driver.resolve(),
               "after":args.candidate_driver.resolve()}
    if not inp.is_file() or inp.stat().st_size == 0 or not all(
            f.is_file() for f in drivers.values()):
        raise RuntimeError("missing or zero-length pinned input / driver")
    compilers = {n:sha(f.parent/"minic-cc") for n,f in drivers.items()}
    if compilers["before"] == compilers["after"]:
        raise RuntimeError("same minic-cc compiler binary; not a genuine A/B")
    args.out.mkdir(parents=True, exist_ok=True)
    records = []
    expected = None
    # Warm each compiler and its input once; exclude warm-up from timing.
    for name in ("before","after"):
        z = one(drivers[name], inp, args.out/(name+"-warm.s"))
        expected = expected or z["sha256"]
        if z["sha256"] != expected:
            raise RuntimeError("warmup output diverged")
    for k in range(4):
        order = ("before","after") if k % 2 == 0 else ("after","before")
        for name in order:
            z = one(drivers[name], inp, args.out/(name+"-"+str(k)+".s"))
            if z["sha256"] != expected:
                raise RuntimeError("assembly mismatch: "+name+" repetition="+str(k))
            records.append({"iteration":k,"mode":name,**z})
    before = statistics.median(z["wall_ms"] for z in records if z["mode"]=="before")
    after = statistics.median(z["wall_ms"] for z in records if z["mode"]=="after")
    result = {"schema":"minic-linux-real-one-owner-paired-v1",
              "input_sha256":sha(inp),"compiler_sha256":compilers,
              "repeat_per_arm":4,"warmup_per_arm":1,
              "assembly_sha256":expected,"assembly_identical":True,
              "baseline_median_ms":before,"candidate_median_ms":after,
              "candidate_speedup":round(before/after,4),"records":records}
    (args.out/"paired-result.json").write_text(json.dumps(result,indent=2)+"\n")
    print("MINIC_ONE_OWNER_AB=PASS real_input_sha256="+result["input_sha256"]+
          " bytes_identical=true repeat=4 before_p50_ms=%.3f after_p50_ms=%.3f speedup=%.4f" %
          (before,after,before/after))
    print("MINIC_ONE_OWNER_AB_SCOPE=ONE_REAL_TU not_full_linux_p1=true")
if __name__ == "__main__":
    main()
