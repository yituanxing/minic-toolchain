#!/usr/bin/env python3
"""Per-ISA 3*7 MiniAS discovery. No assembly success is a parity PASS."""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys

TARGETS = ("x86_64", "aarch64", "loongarch64")
N = 7

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def save(p, x):
    p = Path(p)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(x, indent=2, sort_keys=True) + "\n")

def cmd(args):
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=50)
        return p.returncode, (p.stderr or p.stdout).strip()
    except subprocess.TimeoutExpired:
        return 124, "process-timeout"
    except OSError as e:
        return 127, "process-start-error:" + str(e)

def prepare(a):
    out = Path(a.out)
    if out.exists() and any(out.iterdir()):
        raise ValueError("refuse stale producer output")
    sources = sorted(Path(a.source_root).glob("core_*.c"))[:70]
    if len(sources) < N:
        raise ValueError("too few source files")
    flags = ["-std=gnu11", "-O1", "-ffreestanding", "-fno-stack-protector",
             "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-S"]
    base = [a.compiler]
    if a.target == "loongarch64":
        base += ["--target=loongarch64-linux-gnu"]
    base += flags
    rc, version = cmd([a.compiler, "--version"])
    if rc:
        raise ValueError("compiler not installed: " + version)
    rows = []
    for source in sources:
        dest = out / "assembly" / (source.stem + ".s")
        dest.parent.mkdir(parents=True, exist_ok=True)
        rc, diag = cmd([*base, "-o", str(dest), str(source)])
        row = {"owner": source.name, "source_sha256": sha(source)}
        if rc == 0 and dest.is_file() and dest.stat().st_size:
            row.update({"status": "READY", "assembly": "assembly/" + dest.name,
                        "assembly_sha256": sha(dest)})
        else:
            dest.unlink(missing_ok=True)
            row.update({"status": "PRODUCER_FAIL", "exit_code": rc,
                        "diagnostic": diag[:2000]})
        rows.append(row)
    ready = sum(r["status"] == "READY" for r in rows)
    save(out / "manifest.json", {"version": 1, "target": a.target,
         "compiler": base, "compiler_version": version.splitlines()[0],
         "selected": len(rows), "ready": ready, "sources": rows})
    print(f"MINIAS_DISCOVERY_PREP target={a.target} selected={len(rows)} "
          f"ready={ready} producer_fail={len(rows)-ready}", flush=True)
    if ready < N:
        raise ValueError("fewer than 7 compiler-generated assembly inputs")

def manifest(root, target):
    m = json.loads((root / "manifest.json").read_text())
    if m["version"] != 1 or m["target"] != target:
        raise ValueError("mixed baseline target / schema")
    rows = m["sources"]
    if len(rows) != m["selected"] or len({r["owner"] for r in rows}) != len(rows):
        raise ValueError("missing or duplicate baseline source")
    if sum(r["status"] == "READY" for r in rows) != m["ready"]:
        raise ValueError("invalid baseline counts")
    for r in rows:
        if r["status"] == "PRODUCER_FAIL":
            continue
        if r["status"] != "READY":
            raise ValueError("unknown producer status")
        rel = PurePosixPath(r["assembly"])
        if rel.is_absolute() or ".." in rel.parts:
            raise ValueError("unsafe input path")
        src = root.joinpath(*rel.parts)
        if not src.is_file() or sha(src) != r["assembly_sha256"]:
            raise ValueError("frozen .s hash mismatch: " + str(src))
    return m

def blocker(diag, code):
    if code == 124: return "process-timeout"
    if code == 127: return "process-start-error"
    s = (diag.splitlines() or ["no-diagnostic"])[0].strip()
    s = re.sub(r"^minic-as:\s*", "", s)
    s = re.sub(r":line=\d+", ":line=N", s)
    if s.startswith("unsupported-target:"): return "unsupported-target"
    if s.startswith("unsupported-instruction:"): return "unsupported-instruction"
    if s.startswith("bad-aarch64-operands:"): return "bad-aarch64-operands"
    return s[:160]

def shard(a):
    root, out = Path(a.corpus), Path(a.out)
    m = manifest(root, a.target)
    ready = [r for r in m["sources"] if r["status"] == "READY"]
    owners = [(i, r) for i, r in enumerate(ready) if i % N == a.shard]
    if not owners: raise ValueError("empty shard")
    rows = []
    for i, r in owners:
        obj = out / "objects" / (Path(r["assembly"]).stem + ".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        code, diag = cmd([a.minias, "--target=" + a.target,
                          "-o", str(obj), str(root / r["assembly"])])
        if code == 0 and obj.is_file() and obj.stat().st_size:
            state, first = "ASSEMBLED_UNVERIFIED", "-"
        elif code == 0:
            state, first = "INFRA_ERROR", "success-without-object"
        else:
            state, first = "CANDIDATE_FAIL", blocker(diag, code)
        rows.append({"index": i, "owner": r["owner"], "status": state,
                     "first_blocker": first, "exit_code": code,
                     "diagnostic": diag[:2000]})
    save(out / "results.json", {"version": 1, "target": a.target,
         "shard": a.shard, "manifest_sha256": sha(root / "manifest.json"),
         "expected_ready": len(ready), "records": rows})
    counts = Counter(r["status"] for r in rows)
    print(f"MINIAS_DISCOVERY_SHARD target={a.target} shard={a.shard} "
          f"tested={len(rows)} assembled_unverified={counts['ASSEMBLED_UNVERIFIED']} "
          f"candidate_fail={counts['CANDIDATE_FAIL']} infra={counts['INFRA_ERROR']}")
    if counts["INFRA_ERROR"]: raise ValueError("no output on successful exit")

def aggregate(a):
    files = sorted(Path(a.results).rglob("results.json"))
    selected = (a.target,) if a.target else TARGETS
    expected = len(selected) * N
    if len(files) != expected:
        raise ValueError(f"incomplete shard artifacts {len(files)}/{expected}")
    by_target = {t: {} for t in selected}
    for f in files:
        x = json.loads(f.read_text())
        t, n = x["target"], x["shard"]
        if x["version"] != 1 or t not in by_target or not 0 <= n < N:
            raise ValueError("invalid target / shard identity")
        if n in by_target[t]: raise ValueError("duplicate shard")
        by_target[t][n] = x
    summary = {}
    for target, shards in by_target.items():
        if set(shards) != set(range(N)): raise ValueError("missing shard: " + target)
        ids = {x["manifest_sha256"] for x in shards.values()}
        populations = {x["expected_ready"] for x in shards.values()}
        if len(ids) != 1 or len(populations) != 1:
            raise ValueError("mixed corpus identities: " + target)
        population = populations.pop()
        rows = [r for n in range(N) for r in shards[n]["records"]]
        if sorted(r["index"] for r in rows) != list(range(population)):
            raise ValueError("missing or repeated owner index: " + target)
        if len({r["owner"] for r in rows}) != population:
            raise ValueError("duplicate owner: " + target)
        if any(r["index"] % N != n for n in range(N) for r in shards[n]["records"]):
            raise ValueError("wrong shard membership: " + target)
        statuses = Counter(r["status"] for r in rows)
        causes = Counter(r["first_blocker"] for r in rows
                         if r["status"] == "CANDIDATE_FAIL")
        if statuses["INFRA_ERROR"]: raise ValueError("infrastructure failure")
        summary[target] = {"ready": population, "status": dict(statuses),
                           "first_blockers": dict(causes.most_common()),
                           "manifest_sha256": ids.pop()}
        print(f"MINIAS_DISCOVERY_ARCH target={target} ready={population} "
              f"assembled_unverified={statuses['ASSEMBLED_UNVERIFIED']} "
              f"candidate_fail={statuses['CANDIDATE_FAIL']}")
        for cause, count in causes.most_common(20):
            print(f"MINIAS_DISCOVERY_BLOCKER target={target} n={count} cause={cause}")
    save(a.out, {"version": 1, "architectures": summary})
    print(f"MINIAS_DISCOVERY=COMPLETE targets={len(selected)} "
          f"shards={expected} GNU_ORACLE=NOT_TESTED LINUX_P1=NOT_TESTED")

def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="action", required=True)
    pre = sub.add_parser("prepare")
    pre.add_argument("--target", choices=TARGETS, required=True)
    pre.add_argument("--compiler", required=True)
    pre.add_argument("--source-root", required=True)
    pre.add_argument("--out", required=True)
    run = sub.add_parser("shard")
    run.add_argument("--target", choices=TARGETS, required=True)
    run.add_argument("--shard", type=int, choices=range(N), required=True)
    run.add_argument("--corpus", required=True)
    run.add_argument("--minias", required=True)
    run.add_argument("--out", required=True)
    agg = sub.add_parser("aggregate")
    agg.add_argument("--results", required=True)
    agg.add_argument("--target", choices=TARGETS)
    agg.add_argument("--out", required=True)
    a = p.parse_args()
    try:
        return {"prepare": prepare, "shard": shard,
                "aggregate": aggregate}[a.action](a) or 0
    except (ValueError, KeyError, OSError, json.JSONDecodeError) as err:
        print("MINIAS_DISCOVERY_INFRA_ERROR=" + str(err), file=sys.stderr)
        return 2

if __name__ == "__main__":
    sys.exit(main())
