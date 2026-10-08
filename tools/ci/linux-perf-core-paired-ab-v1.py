#!/usr/bin/env python3
"""Paired Core-workspace A/B on one frozen Linux first500 corpus.

Each translation unit is compiled by both compilers on the same runner.
Pair order alternates by selected index to balance warm-page-cache effects.
Exactly four TU-pairs are processed concurrently.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", type=Path, required=True)
    ap.add_argument("--baseline", type=Path, required=True)
    ap.add_argument("--candidate", type=Path, required=True)
    ap.add_argument("--work", type=Path, required=True)
    ap.add_argument("--jobs", type=int, default=4)
    args = ap.parse_args()
    if args.jobs != 4:
        raise SystemExit("four-worker A/B is required")
    selected = args.corpus / "selected-tus.txt"
    rows = []
    for line in selected.read_text().splitlines():
        if not line.strip():
            continue
        index, obj, rel_i, source = line.split("\t", 3)
        rows.append((int(index), obj, rel_i, source))
    if len(rows) != 500 or len({r[0] for r in rows}) != 500:
        raise SystemExit(f"expected 500 unique selected translation units, got {len(rows)}")
    args.work.mkdir(parents=True, exist_ok=True)
    for name in ("baseline", "candidate"):
        (args.work / name).mkdir(parents=True, exist_ok=True)

    def compile_one(name, binary, row):
        index, _, rel_i, _ = row
        input_path = args.corpus / "kbuild" / rel_i
        output_path = args.work / name / (rel_i + ".s")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        if not input_path.is_file() or input_path.stat().st_size == 0:
            return {"status": "PREPROCESS_MISSING", "seconds": 0, "sha256": None,
                    "error": f"missing {input_path}"}
        t0 = time.monotonic()
        proc = subprocess.run([str(binary), "-S", str(input_path), "-o", str(output_path)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              text=True, errors="replace", check=False)
        elapsed = time.monotonic() - t0
        good = proc.returncode == 0 and output_path.is_file() and output_path.stat().st_size != 0
        return {"status": "PASS" if good else "FAIL", "seconds": elapsed,
                "sha256": hashlib.sha256(output_path.read_bytes()).hexdigest() if good else None,
                "error": "" if good else proc.stderr[-1000:] or f"returncode={proc.returncode}"}

    def compile_pair(row):
        first, second = (("baseline", args.baseline), ("candidate", args.candidate))
        if row[0] % 2:
            first, second = second, first
        a = compile_one(first[0], first[1], row)
        b = compile_one(second[0], second[1], row)
        results = {first[0]: a, second[0]: b}
        return {"index": row[0], "object": row[1], "input": row[2], "source": row[3],
                **results, "assembly_identical": (a["status"] == "PASS" and
                b["status"] == "PASS" and a["sha256"] == b["sha256"])}

    begun = time.monotonic()
    output = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_pair, row) for row in rows]
        for i, future in enumerate(as_completed(futures), 1):
            output.append(future.result())
            if i % 25 == 0 or i == len(rows):
                print(f"PAIRED_AB_PROGRESS {i}/{len(rows)}", flush=True)
    wall_seconds = time.monotonic() - begun
    output.sort(key=lambda r: r["index"])
    (args.work / "paired-results.json").write_text(json.dumps(output, indent=2))
    good = [r for r in output if r["assembly_identical"]]
    digest_rows = [f'{r["index"]}\t{r["input"]}\t{r["candidate"]["sha256"]}'
                   for r in good]
    (args.work / "output-fingerprints.tsv").write_text("\n".join(digest_rows) + "\n")
    print(f"PAIRED_AB selected=500 matched={len(good)} mismatched={500-len(good)} "
          f"jobs=4 wall_seconds={wall_seconds:.3f}", flush=True)
    for name in ("baseline", "candidate"):
        passed = [float(r[name]["seconds"]) for r in output if r[name]["status"] == "PASS"]
        print(f"PAIRED_AB_{name.upper()} pass={len(passed)} "
              f"sum_tu_seconds={sum(passed):.3f} "
              f"median_tu_seconds={statistics.median(passed) if passed else 0:.4f}",
              flush=True)
    if len(good) != 500:
        for r in output:
            if not r["assembly_identical"]:
                print(f"PAIRED_AB_MISMATCH index={r['index']} input={r['input']} "
                      f"baseline={r['baseline']} candidate={r['candidate']}", flush=True)
        raise SystemExit(1)
    old = sum(float(r["baseline"]["seconds"]) for r in output)
    new = sum(float(r["candidate"]["seconds"]) for r in output)
    print(f"PAIRED_AB_SPEEDUP={old/new:.4f} "
          f"BASELINE_SUM={old:.3f} CANDIDATE_SUM={new:.3f}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
