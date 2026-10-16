#!/usr/bin/env python3
"""Strict binary-output equivalence gate for an existing SHA-frozen Linux .i corpus.

Two MiniC drivers are run against exactly the same preprocessed inputs.
Every selected TU must compile successfully and generate identical assembly.
This is NOT Linux QEMU certification or a throughput A/B experiment.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def selected_inputs(corpus):
    manifest = corpus / "selected-tus.txt"
    if not manifest.is_file():
        raise RuntimeError("frozen selected-tus.txt missing")
    lines = manifest.read_text().splitlines()
    if len(lines) != 500:
        raise RuntimeError(f"expected 500 selected TUs; found {len(lines)}")
    base = (corpus / "kbuild").resolve()
    records = []
    indices = set()
    for line in lines:
        fields = line.split("\t", 3)
        if len(fields) != 4:
            raise RuntimeError("malformed selected TU row")
        number, obj, rel_i, _source = fields
        index = int(number)
        if index in indices:
            raise RuntimeError("duplicate selected index")
        indices.add(index)
        file = (base / rel_i).resolve()
        if not file.is_relative_to(base) or not file.is_file() or file.stat().st_size == 0:
            raise RuntimeError("missing or out-of-scope frozen input " + rel_i)
        records.append((index, obj, rel_i, file))
    return records


def compile_one(exe, source, target):
    start = time.perf_counter_ns()
    try:
        proc = subprocess.run([str(exe), "-S", str(source), "-o", str(target)],
                              capture_output=True, text=True, errors="replace",
                              timeout=150, check=False)
    except subprocess.TimeoutExpired:
        return {"status": "TIMEOUT", "stderr": "compiler exceeded 150 s"}
    elapsed = round((time.perf_counter_ns() - start) / 1e6, 3)
    if proc.returncode or not target.is_file() or target.stat().st_size == 0:
        return {"status": "FAIL", "stderr": proc.stderr[-1200:], "rc": proc.returncode}
    return {"status": "PASS", "sha256": digest(target),
            "bytes": target.stat().st_size, "wall_ms": elapsed}


def one_pair(entry, old, new, balanced_order=False):
    index, obj, rel_i, source = entry
    order = ("baseline", "candidate") if not balanced_order or index % 2 == 0 else ("candidate", "baseline")
    with tempfile.TemporaryDirectory(prefix="minic-pair-") as directory:
        folder = Path(directory)
        calls = {"baseline": (old, folder / "old.s"),
                 "candidate": (new, folder / "new.s")}
        results = {arm: compile_one(calls[arm][0], source, calls[arm][1])
                   for arm in order}
    baseline, candidate = results["baseline"], results["candidate"]
    equivalent = (baseline["status"] == "PASS" and
                  candidate["status"] == "PASS" and
                  baseline["sha256"] == candidate["sha256"])
    return {"index": index, "object": obj, "input": rel_i,
            "first": order[0], "baseline": baseline, "candidate": candidate,
            "assembly_identical": equivalent}


def main():
    cli = argparse.ArgumentParser()
    for name in ("corpus", "baseline_driver", "candidate_driver", "out"):
        cli.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    cli.add_argument("--jobs", type=int, choices=(1, 2, 4), default=4)
    cli.add_argument("--balanced-order", action="store_true",
                     help="alternate which compiler runs first by pinned TU index")
    args = cli.parse_args()
    corpus, out = args.corpus.resolve(), args.out.resolve()
    old, new = args.baseline_driver.resolve(), args.candidate_driver.resolve()
    for bin_path in (old, new, old.parent / "minic-cc", new.parent / "minic-cc"):
        if not bin_path.is_file():
            raise RuntimeError("missing compiler executable or minic-cc " + str(bin_path))
    old_sha, new_sha = digest(old.parent / "minic-cc"), digest(new.parent / "minic-cc")
    if old_sha == new_sha:
        raise RuntimeError("identical old/new compiler code: false differential")
    records = selected_inputs(corpus)
    out.mkdir(parents=True, exist_ok=True)
    all_rows = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        work = [pool.submit(one_pair, row, old, new, args.balanced_order) for row in records]
        for index, future in enumerate(as_completed(work), 1):
            row = future.result()
            all_rows.append(row)
            if not row["assembly_identical"]:
                print("MINIC_FROZEN_DIFF=FAIL " + json.dumps(row, ensure_ascii=False),
                      flush=True)
            if index % 50 == 0:
                print(f"MINIC_FROZEN_DIFF_PROGRESS checked={index}/500", flush=True)
    all_rows.sort(key=lambda x: x["index"])
    mismatches = [row for row in all_rows if not row["assembly_identical"]]
    results = {"schema": "minic-frozen-corpus-assembly-diff-v1",
               "input_count": len(all_rows), "baseline_cc_sha256": old_sha,
               "candidate_cc_sha256": new_sha,
               "identical": len(all_rows) - len(mismatches),
               "mismatch_count": len(mismatches),
               "balanced_order": args.balanced_order,
               "results": all_rows}
    (out / "differential.json").write_text(
        json.dumps(results, indent=2, ensure_ascii=False) + "\n")
    if mismatches:
        raise SystemExit(f"MINIC_FROZEN_DIFF=FAIL mismatches={len(mismatches)}")
    before_ms = sum(row["baseline"]["wall_ms"] for row in all_rows)
    after_ms = sum(row["candidate"]["wall_ms"] for row in all_rows)
    regressions = sum(row["candidate"]["wall_ms"] > row["baseline"]["wall_ms"]
                      for row in all_rows)
    print("MINIC_FROZEN_DIFF=PASS selected=500 identical=500 "
          "runtime_p1_not_proven=true")
    print("MINIC_FROZEN_PAIRED=PASS owners=500 balanced_order=%s "
          "baseline_total_ms=%.3f candidate_total_ms=%.3f "
          "candidate_speedup=%.5f regressions=%d" %
          (str(args.balanced_order).lower(), before_ms, after_ms,
           before_ms / after_ms, regressions))


if __name__ == "__main__":
    main()
