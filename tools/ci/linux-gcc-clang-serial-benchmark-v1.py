#!/usr/bin/env python3
"""Serial, per-translation-unit MiniC/GCC/Clang -S Linux corpus benchmark.

The single-threaded metric is measured end-to-end process wall time, not
dividing 4-worker latency by four. All compiler families consume the SAME
Kbuild GCC preprocessed .i and emit RV64 assembler to the SAME output path.
This is a front-end/codegen throughput comparison, NOT a comparison of
optimized machine-code quality or complete Kbuild performance.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import statistics
import subprocess
import time

def main():
    a = argparse.ArgumentParser()
    a.add_argument("--minic", required=True)
    a.add_argument("--corpus", required=True, type=Path)
    a.add_argument("--out", required=True, type=Path)
    a.add_argument("--expected-offset", required=True, type=int)
    a.add_argument("--expected-count", required=True, type=int)
    args = a.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    input_root=args.corpus/"kbuild"
    rows=[s.split("\t",3) for s in (args.corpus/"selected-tus.txt").read_text().splitlines() if s.strip()]
    indices=[int(r[0]) for r in rows]
    assert indices == list(range(args.expected_offset,args.expected_offset+args.expected_count)),(indices[:5], len(rows))
    base=["-S","-x","cpp-output","-ffreestanding","-fno-pie","-fno-pic",
          "-mcmodel=medany","-march=rv64gc","-mabi=lp64d"]
    modes={
      "MiniC": [args.minic,"-S"],
      "GCC_O0": ["riscv64-linux-gnu-gcc",*base,"-O0"],
      "GCC_O2": ["riscv64-linux-gnu-gcc",*base,"-O2"],
      "Clang_O0": ["clang","--target=riscv64-linux-gnu",*base,"-O0"],
      "Clang_O2": ["clang","--target=riscv64-linux-gnu",*base,"-O2"],
    }
    all_results=[]
    output=args.out/"output.s"
    for mode,prefix in modes.items():
        started=time.monotonic()
        times=[]
        states=Counter()
        results=[]
        print(f"COMPARE_SERIAL_BEGIN mode={mode} selected={len(rows)}",flush=True)
        for ordinal,(index,_,rel_i,_) in enumerate(rows,1):
            src=input_root/rel_i
            if not src.is_file() or src.stat().st_size==0:
                status="MISSING"; elapsed=0.0; diagnostic="missing GCC preprocessed input"
            else:
                output.unlink(missing_ok=True)
                cmd=prefix+[str(src),"-o",str(output)]
                tic=time.monotonic()
                try:
                    proc=subprocess.run(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,
                                        timeout=240,text=True,errors="replace",check=False)
                    elapsed=time.monotonic()-tic
                    status="PASS" if proc.returncode==0 and output.is_file() and output.stat().st_size>0 else "FAIL"
                    diagnostic=proc.stderr.strip().splitlines()[-1][:500] if proc.stderr.strip() else ""
                except subprocess.TimeoutExpired:
                    elapsed=time.monotonic()-tic
                    status="TIMEOUT";diagnostic="240s per input deadline"
            times.append(elapsed);states[status]+=1
            results.append(dict(index=int(index),input=rel_i,mode=mode,seconds=round(elapsed,6),
                                status=status,diagnostic=diagnostic))
            if ordinal%100==0 or ordinal==len(rows):
                print(f"COMPARE_SERIAL_PROGRESS mode={mode} done={ordinal}/{len(rows)}"
                      f" sum_s={sum(times):.3f} failed={states['FAIL']+states['TIMEOUT']}",flush=True)
        mode_summary=dict(offset=args.expected_offset,count=len(rows),mode=mode,
                          sum_tu_seconds=round(sum(times),3),
                          serial_wall_seconds=round(time.monotonic()-started,3),
                          pass_count=states['PASS'],fail_count=states['FAIL'],
                          missing_count=states['MISSING'],timeout_count=states['TIMEOUT'],
                          median_seconds=round(statistics.median(times),4),
                          slowest=sorted(results,key=lambda x:x["seconds"],reverse=True)[:5])
        print("COMPARE_SERIAL_SHARD "+json.dumps(mode_summary,sort_keys=True),flush=True)
        all_results.extend(results)
        output.unlink(missing_ok=True)
    (args.out/"results.json").write_text(json.dumps(all_results,indent=1))
    summary={m:dict(count=sum(r["mode"]==m for r in all_results),
                    pass_count=sum(r["mode"]==m and r["status"]=="PASS" for r in all_results),
                    sum_tu_seconds=round(sum(r["seconds"] for r in all_results if r["mode"]==m),3))
             for m in modes}
    (args.out/"summary.json").write_text(json.dumps(dict(offset=args.expected_offset,count=len(rows),modes=summary),indent=2))
    print("COMPARE_SERIAL_ALL_MODES_RECORDED=PASS",flush=True)
if __name__=="__main__":
    main()
