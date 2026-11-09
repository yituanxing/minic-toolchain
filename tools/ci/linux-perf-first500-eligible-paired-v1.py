#!/usr/bin/env python3
"""Honest same-runner A/B audit over authentic frozen GNU Linux first500.

Unsupported inputs are recorded, not hidden as successful compilations. This
reports performance only for owners both compilers accept with byte-identical
assembly. Any candidate-only reject or assembly change fails after all 500.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import time

from linux_kbuild_language_flags import RV64_LINUX_KBUILD_LANGUAGE_FLAGS


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def compile_owner(compiler, source, output):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter_ns()
    try:
        process = subprocess.run(
            [str(compiler), *RV64_LINUX_KBUILD_LANGUAGE_FLAGS,
             "-S", str(source), "-o", str(output)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=180, check=False)
        rc = process.returncode
        stderr = process.stderr.decode(errors="replace")[-1300:]
    except subprocess.TimeoutExpired:
        rc = 124
        stderr = "timed out after 180 seconds"
    wall_ms = (time.perf_counter_ns() - start) / 1e6
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    valid = rc == 0 and output.is_file() and output.stat().st_size > 0
    return {
        "accepted": valid, "returncode": rc,
        "wall_ms": round(wall_ms, 3),
        "user_ms": round((after.ru_utime - before.ru_utime) * 1000, 3),
        "sys_ms": round((after.ru_stime - before.ru_stime) * 1000, 3),
        "assembly_sha256": sha(output) if valid else None,
        "assembly_bytes": output.stat().st_size if valid else None,
        "stderr_tail": stderr if not valid else "",
    }


def main():
    p = argparse.ArgumentParser()
    for name in ("corpus", "baseline", "candidate", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    args = p.parse_args()
    corpus, before, after, output = [
        v.resolve() for v in
        (args.corpus, args.baseline, args.candidate, args.output)]
    if not before.is_file() or not after.is_file() or sha(before) == sha(after):
        raise SystemExit("missing baseline/candidate or identical binaries")
    manifest = corpus / "selected-tus.txt"
    rows = manifest.read_text().splitlines()
    if len(rows) != 500:
        raise SystemExit("frozen Linux first500 has wrong manifest length")
    hashes = corpus / "inputs.sha256"
    if len(hashes.read_text().splitlines()) != 500:
        raise SystemExit("frozen Linux first500 has wrong digest count")
    subprocess.run(["sha256sum", "--status", "--check", str(hashes)], check=True)
    output.mkdir(parents=True, exist_ok=True)
    work = output / "intermediates"
    work.mkdir(exist_ok=True)
    root = (corpus / "kbuild").resolve()
    names = set()
    details = []
    totals = {"both_pass": 0, "both_fail": 0, "both_no_assembly": 0,
              "candidate_only_fail": 0, "candidate_only_pass": 0,
              "assembly_mismatch": 0}
    before_wall = after_wall = before_cpu = after_cpu = 0.0
    for n, row in enumerate(rows):
        fields = row.split("\t", 3)
        if len(fields) != 4:
            raise SystemExit("invalid GNU Kbuild owner manifest entry")
        index, owner, relative, unit = fields
        if owner in names or not index.isdigit() or not unit:
            raise SystemExit("duplicate owner or invalid source unit")
        names.add(owner)
        source = (root / relative).resolve()
        if not source.is_relative_to(root) or not source.is_file():
            raise SystemExit("unsafe or missing frozen .i: " + str(source))
        dirs = {"baseline": before, "candidate": after}
        parity = hashlib.sha256(owner.encode()).digest()[0] & 1
        order = ("baseline", "candidate") if parity == 0 else ("candidate", "baseline")
        measured = {}
        for arm in order:
            target = work / (index.zfill(5) + "-" + arm + ".s")
            if target.exists():
                raise SystemExit("stale A/B assembly output: " + str(target))
            measured[arm] = compile_owner(dirs[arm], source, target)
        a, b = measured["baseline"], measured["candidate"]
        if a["accepted"] and b["accepted"]:
            if a["assembly_sha256"] == b["assembly_sha256"]:
                classification = "both_pass"
                before_wall += a["wall_ms"]
                after_wall += b["wall_ms"]
                before_cpu += a["user_ms"] + a["sys_ms"]
                after_cpu += b["user_ms"] + b["sys_ms"]
            else:
                classification = "assembly_mismatch"
        elif not a["accepted"] and not b["accepted"]:
            # A source may legitimately produce no materialized .s even when
            # both compiler processes exit 0. Track this separately from
            # actual compiler rejection; neither is a comparable timing pair.
            classification = ("both_no_assembly"
                              if a["returncode"] == 0 and b["returncode"] == 0
                              else "both_fail")
        elif a["accepted"]:
            classification = "candidate_only_fail"
        else:
            classification = "candidate_only_pass"
        totals[classification] += 1
        details.append({"index": index, "owner": owner, "source": relative,
                        "first": order[0], "classification": classification,
                        **measured})
        if (n + 1) % 25 == 0:
            print("MINIC_FIRST500_AUDIT_PROGRESS=" + str(n + 1) +
                  "/500 accepted_both=" + str(totals["both_pass"]) +
                  " inherited_fail=" + str(totals["both_fail"]), flush=True)
    speed = round(before_wall / after_wall, 5) if after_wall > 0 else None
    cpu_speed = round(before_cpu / after_cpu, 5) if after_cpu > 0 else None
    result = {
        "schema": "minic-real-linux-first500-eligibility-paired-v1",
        "scope": "Only both-pass unchanged-assembly subset is timed",
        "corpus_sha256_manifest": sha(hashes), "owners": len(details),
        "kbuild_language_flags": list(RV64_LINUX_KBUILD_LANGUAGE_FLAGS),
        "baseline_sha256": sha(before), "candidate_sha256": sha(after),
        "status_counts": totals,
        "eligible_baseline_wall_ms": round(before_wall, 3),
        "eligible_candidate_wall_ms": round(after_wall, 3),
        "eligible_wall_speedup": speed,
        "eligible_cpu_speedup": cpu_speed,
        "performance_gate_eligible_only": "PASS" if speed is not None and speed >= 1.04 else "STOP",
        "introduced_breakages": [
            {"owner": d["owner"], "kind": d["classification"],
             "baseline_stderr": d["baseline"]["stderr_tail"],
             "candidate_stderr": d["candidate"]["stderr_tail"]}
            for d in details
            if d["classification"] in ("candidate_only_fail", "assembly_mismatch")
        ],
        "inherited_failure_examples": [
            {"owner": d["owner"],
             "baseline_rc": d["baseline"]["returncode"],
             "candidate_rc": d["candidate"]["returncode"],
             "baseline_stderr": d["baseline"]["stderr_tail"]}
            for d in details if d["classification"] == "both_fail"
        ][:12],
        "no_assembly_examples": [
            {"owner": d["owner"],
             "baseline_rc": d["baseline"]["returncode"],
             "candidate_rc": d["candidate"]["returncode"]}
            for d in details if d["classification"] == "both_no_assembly"
        ][:12],
    }
    (output / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    (output / "details.json").write_text(json.dumps(details, indent=2) + "\n")
    print("MINIC_FIRST500_AUDIT=COMPLETE owners=500 " +
          " ".join(k + "=" + str(v) for k,v in totals.items()) +
          " eligible_wall_speedup=" + str(speed) +
          " eligible_cpu_speedup=" + str(cpu_speed), flush=True)
    if totals["candidate_only_fail"] or totals["assembly_mismatch"]:
        raise SystemExit("NEW_COMPILER_REGRESSION: see summary.json")
    if totals["both_pass"] == 0:
        raise SystemExit("NO_ELIGIBLE_LINUX_OWNERS: cannot assess performance")
    print("MINIC_FIRST500_AUDIT=PASS inherited_rejections_recorded=" +
          str(totals["both_fail"]) +
          " perf_gate_eligible_only=" + result["performance_gate_eligible_only"],
          flush=True)


if __name__ == "__main__":
    main()
