#!/usr/bin/env python3
"""Merge seven actual Kbuild MiniC compiler-only A/B shards.

Entry point for 2064-file first-class performance acceptance; require all seven
real uncached producer shards rather than using historical object cache hits.

Do not confuse sum of per-TU times with seven-runner elapsed wall time.
Each file was run on the same GNU-generated .i, old and new compilers,
with exact assembly output equality verified before its row was written.
"""
import argparse
import json
from pathlib import Path
import statistics

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--artifacts",type=Path,required=True)
    p.add_argument("--out",type=Path,required=True)
    p.add_argument("--expect",type=int,default=2064)
    a=p.parse_args()
    folders=sorted(a.artifacts.glob("minic-perf-2064-ab-shard-*/"))
    if len(folders)!=7:
        raise RuntimeError(f"Expected 7 shard artifacts, found {len(folders)}")
    rows=[]
    per_shard=[]
    seen=set()
    for folder in folders:
        tsv=folder/"perf-ab.tsv"
        summary=folder/"perf-ab-summary.json"
        if not tsv.is_file() or not summary.is_file():
            raise RuntimeError("Missing A/B evidence in "+str(folder))
        original=json.loads(summary.read_text())
        local=[]
        for line in tsv.read_text().splitlines():
            f=line.split("\t")
            if len(f)!=6:
                raise RuntimeError("Bad shard line: "+str(folder))
            owner,old,new,nbytes,sha,first=f
            old=float(old)
            new=float(new)
            if owner in seen or len(sha)!=64 or old<=0 or new<=0 or first not in ("baseline","candidate"):
                raise RuntimeError("Duplicated/invalid profiler entry: "+owner)
            seen.add(owner)
            local.append({"owner":owner,"baseline_ms":old,"candidate_ms":new,
                          "input_bytes":int(nbytes),"asm_sha256":sha,"first":first})
        if len(local)!=original["objects"]:
            raise RuntimeError("Shard row count differs from authenticated per-shard summary")
        if abs(sum(x["baseline_ms"] for x in local)-original["baseline_total_ms"])>0.1:
            raise RuntimeError("Shard old total checksum mismatch")
        if abs(sum(x["candidate_ms"] for x in local)-original["candidate_total_ms"])>0.1:
            raise RuntimeError("Shard new total checksum mismatch")
        old=sum(x["baseline_ms"] for x in local)
        new=sum(x["candidate_ms"] for x in local)
        per_shard.append({"name":folder.name,"count":len(local),"baseline_total_ms":round(old,3),
                          "candidate_total_ms":round(new,3),"speedup":round(old/new,4)})
        rows+=local
    if len(rows)!=a.expect:
        raise RuntimeError(f"Full Linux A/B incomplete: expected {a.expect}, got {len(rows)}")
    before=sum(x["baseline_ms"] for x in rows)
    after=sum(x["candidate_ms"] for x in rows)
    aggregate={
        "schema":"minic-real-linux-paired-2064-aggregate-v1",
        "measurement":"sum of 2064 .i-to-.s compiler-only per-TU wall durations",
        "NOT_seven_runner_wall":True,
        "files":len(rows),"baseline_total_ms":round(before,3),
        "candidate_total_ms":round(after,3),
        "speedup":round(before/after,5),
        "reduction_pct":round(100*(1-after/before),3),
        "baseline_first":sum(x["first"]=="baseline" for x in rows),
        "candidate_first":sum(x["first"]=="candidate" for x in rows),
        "regressions":sum(x["candidate_ms"]>x["baseline_ms"] for x in rows),
        "p50_baseline_ms":round(statistics.median(x["baseline_ms"] for x in rows),3),
        "p50_candidate_ms":round(statistics.median(x["candidate_ms"] for x in rows),3),
        "shards":per_shard,
        "slowest_candidate":sorted(rows,key=lambda x:x["candidate_ms"],reverse=True)[:25],
        "biggest_savings":sorted(rows,key=lambda x:x["baseline_ms"]-x["candidate_ms"],reverse=True)[:25],
        "biggest_regressions":sorted(rows,key=lambda x:x["candidate_ms"]-x["baseline_ms"],reverse=True)[:25]
    }
    a.out.mkdir(parents=True,exist_ok=True)
    (a.out/"aggregate.json").write_text(json.dumps(aggregate,indent=2)+"\n")
    report=[
        "## MiniC 实际 Linux 2064 文件编译速度 A/B",
        "",
        "| 指标 | 数值 |",
        "|---|---:|",
        f"| 编译文件数 | {len(rows)} |",
        f"| 旧 MiniC 累计 .i→.s | {before/1000:.3f} s |",
        f"| 三项联合优化 MiniC 累计 .i→.s | {after/1000:.3f} s |",
        f"| 整体加速比（累计） | **{before/after:.4f}×** |",
        f"| 总时间下降 | **{100*(1-after/before):.2f}%** |",
        f"| 出现单文件回退 | {aggregate['regressions']} |",
        "",
        "**说明**：两个数字是相同 2064 个 `.i` 在相同七路 CI 环境中逐文件的新旧编译器耗时之和，"
        "不包括 GNU 预处理/汇编，也不能冒充并行七 runner 实际墙钟时间。每个文件汇编 SHA 均已验证一致。",
        "",
        "### 当前最慢的 10 个 Linux 文件",
        "| 文件 | 旧版 ms | 新版 ms | 加速比 |",
        "|---|---:|---:|---:|"
    ]
    for row in aggregate["slowest_candidate"][:10]:
        old=row["baseline_ms"]
        new=row["candidate_ms"]
        report.append(f"| `{row['owner']}` | {old:.1f} | {new:.1f} | {old/new:.3f}× |")
    report+=["","### 七路分片累计时间（不是各 runner 墙钟）",
             "| 分片 | 文件 | 旧版累计秒 | 新版累计秒 | 加速比 |",
             "|---|---:|---:|---:|---:|"]
    for x in per_shard:
        report.append(f"| {x['name']} | {x['count']} | {x['baseline_total_ms']/1000:.1f} | {x['candidate_total_ms']/1000:.1f} | {x['speedup']:.3f}× |")
    (a.out/"summary.md").write_text("\n".join(report)+"\n")
    print(f"MINIC_LINUX_2064_PERF=PASS count={len(rows)} old_seconds={before/1000:.3f} "
          f"new_seconds={after/1000:.3f} speedup={before/after:.5f} reduction_pct={100*(1-after/before):.3f}")
    print("MINIC_LINUX_2064_PERF_SCOPE=SUM_TU_COMPILER_TIME not_parallel_ci_wall=true")

if __name__=="__main__":
    main()
