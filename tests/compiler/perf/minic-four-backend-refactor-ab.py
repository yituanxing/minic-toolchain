#!/usr/bin/env python3
"""Paired, immutable-source MiniC refactor benchmark; fail on correctness, not speed.

Uses identical preprocessed original vendor code across both binaries and four
target triples. Measures serial ABBA order after explicit warmup; publishes
wall/user/sys CPU, compiler identity, per-target assembly hashes, and x86 native
runtime differential. Output identity is observed, not falsely assumed.
This is a one-TU acceptance sample, not a Linux 2064-file performance claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import statistics
import subprocess
import time


TARGETS = ("x86_64-linux", "riscv64-linux", "aarch64-linux", "loongarch64-linux")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def compile_one(driver, target, source, asm):
    pre = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter_ns()
    result = subprocess.run(
        [str(driver), "--target=" + target, "-S", str(source), "-o", str(asm)],
        capture_output=True, text=True, timeout=180, errors="replace",
    )
    wall = (time.perf_counter_ns() - start) / 1e6
    post = resource.getrusage(resource.RUSAGE_CHILDREN)
    if result.returncode or not asm.is_file() or not asm.stat().st_size:
        raise RuntimeError(
            "MiniC compilation failed target=" + target +
            " driver=" + str(driver) + " exit=" + str(result.returncode) +
            " stderr=" + result.stderr[-1600:]
        )
    return {
        "wall_ms": round(wall, 3),
        "user_ms": round(1000 * (post.ru_utime - pre.ru_utime), 3),
        "sys_ms": round(1000 * (post.ru_stime - pre.ru_stime), 3),
        "assembly_bytes": asm.stat().st_size,
        "assembly_sha256": sha256(asm),
    }


def native_observable(asm, executable):
    subprocess.run(
        ["cc", "-no-pie", "-x", "assembler", str(asm), "-o", str(executable)],
        check=True, capture_output=True, text=True, timeout=60,
    )
    p = subprocess.run(
        [str(executable)], capture_output=True, timeout=45,
    )
    return {
        "returncode": p.returncode,
        "stdout_sha256": hashlib.sha256(p.stdout).hexdigest(),
        "stderr_sha256": hashlib.sha256(p.stderr).hexdigest(),
    }


def main():
    cli = argparse.ArgumentParser()
    for name in ("baseline", "candidate", "source", "output"):
        cli.add_argument("--" + name, type=Path, required=True)
    cli.add_argument("--repeats", type=int, choices=(2, 4), default=4)
    args = cli.parse_args()
    baseline, candidate, source = (
        args.baseline.resolve(), args.candidate.resolve(), args.source.resolve()
    )
    assert all(x.is_file() and x.stat().st_size for x in (baseline, candidate, source))
    compilers = {"before": baseline, "after": candidate}
    identities = {k: sha256(v) for k, v in compilers.items()}
    if identities["before"] == identities["after"]:
        raise RuntimeError("before/after minic-cc binaries are identical; invalid experiment")
    args.output.mkdir(parents=True, exist_ok=True)
    outcomes = []
    rows = [
        "# MiniC four-ISA original AES source: paired refactor A/B",
        "",
        "Baseline and candidate are separate compiler builds. The exact same "
        "preprocessed original AES-128 ECB C source is supplied to both.",
        "",
        "Input SHA-256: \`" + sha256(source) + "\`",
        "",
        "| Target | Baseline wall p50 (ms) | Candidate wall p50 (ms) | "
        "Speedup | Assembly bytes identical |",
        "| :-- | --: | --: | --: | :-- |",
    ]
    for target in TARGETS:
        tdir = args.output / target
        tdir.mkdir(parents=True, exist_ok=True)
        samples = []
        first_asm = {}
        for phase in ("before", "after"):
            asm = tdir / (phase + "-warm.s")
            first_asm[phase] = asm
            compile_one(compilers[phase], target, source, asm)
        for iteration in range(args.repeats):
            order = ("before", "after") if iteration % 2 == 0 else ("after", "before")
            for phase in order:
                record = compile_one(
                    compilers[phase], target, source,
                    tdir / (phase + "-" + str(iteration) + ".s"),
                )
                samples.append({"iteration": iteration, "phase": phase, **record})
        baseline_wall = statistics.median(
            r["wall_ms"] for r in samples if r["phase"] == "before")
        candidate_wall = statistics.median(
            r["wall_ms"] for r in samples if r["phase"] == "after")
        before_sha = sha256(first_asm["before"])
        after_sha = sha256(first_asm["after"])
        identical = before_sha == after_sha
        entry = {
            "target": target,
            "source_sha256": sha256(source),
            "baseline_p50_ms": baseline_wall,
            "candidate_p50_ms": candidate_wall,
            "speedup": round(baseline_wall / candidate_wall, 4),
            "baseline_assembly_sha256": before_sha,
            "candidate_assembly_sha256": after_sha,
            "assembly_identical": identical,
            "samples": samples,
        }
        if target == "x86_64-linux":
            native = {
                phase: native_observable(path, tdir / (phase + ".exe"))
                for phase, path in first_asm.items()
            }
            if native["before"] != native["after"] or native["before"]["returncode"] != 0:
                raise RuntimeError("x86-64 original AES runtime differential: " + str(native))
            entry["x86_native_runtime"] = native
        outcomes.append(entry)
        rows.append(
            f"| {target} | {baseline_wall:.3f} | {candidate_wall:.3f} | "
            f"{baseline_wall/candidate_wall:.4f}× | {'yes' if identical else 'no'} |"
        )
        print(
            f"MINIC_REFACTOR_AB target={target} baseline_ms={baseline_wall:.3f} "
            f"candidate_ms={candidate_wall:.3f} speedup={baseline_wall/candidate_wall:.4f} "
            f"assembly_identical={identical}", flush=True
        )

    data = {
        "schema": "minic-four-isa-refactor-paired-original-aes-v1",
        "scope": "one original C source, 4 ISAs, serial ABBA, not full Linux",
        "compiler_sha256": identities,
        "source_sha256": sha256(source),
        "repeats_per_compiler_per_isa": args.repeats,
        "warmups_per_compiler_per_isa": 1,
        "results": outcomes,
    }
    (args.output / "results.json").write_text(json.dumps(data, indent=2) + "\n")
    rows += [
        "",
        "Native x86-64 baseline/candidate AES runtime exit=0 and stdout/stderr "
        "SHA-256 identical. Non-x86 outputs are compile-level A/B only: "
        "GNU/QEMU runtime is covered by a separate focused suite.",
        "",
        "Interpretation: wall time is susceptible to hosted-runner contention; "
        "CPU timing samples and per-ISA assembly hashes are retained in results.json. "
        "This is **not** a representative 2064-file Linux throughput claim.",
    ]
    (args.output / "summary.md").write_text("\n".join(rows) + "\n")
    print("MINIC_REFACTOR_PAIRED_AB=PASS source=original_AES "
          "targets=4 x86_native_runtime=equal", flush=True)


if __name__ == "__main__":
    main()
