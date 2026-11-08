#!/usr/bin/env python3
"""Paired, immutable-source MiniC refactor benchmark; fail on correctness, not speed.

Uses identical preprocessed original vendor code across both binaries and four
or (opt-in) five target triples. Measures serial ABBA order after explicit
warmup; publishes wall/user/sys CPU, compiler identity, deterministic assembly
size/opcode-line counts, hashes, and x86 native runtime differential.
Opcode-line counts are textual estimates, not decoded machine instruction counts.
This is a one-TU acceptance sample, not a Linux throughput performance claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import statistics
import subprocess
import time


TARGETS = ("x86_64-linux", "riscv64-linux", "aarch64-linux", "loongarch64-linux")
FIVE_TARGETS = ("riscv32-linux",) + TARGETS
TARGET_OPTIONS = {"riscv32-linux": ("-march=rv32i", "-mabi=ilp32")}


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def asm_opcode_lines(path):
    """Count textual opcode lines, excluding labels/directives/comments.

    Includes assembler pseudo-ops that are spelled like instructions and does
    not decode assembler macro expansion; never equate this with dynamic retired
    instructions or machine code bytes.
    """
    count = 0
    for line in path.read_text(errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith(("#", "//", ";", "@", "/*", "*")):
            continue
        # Accept GNU local labels on the same line as an instruction.
        line = re.sub(r"^(?:[.$\w]+:\s*)+", "", line)
        if line.startswith("."):
            continue
        if re.match(r"^[a-zA-Z][a-zA-Z_0-9.]*\b", line):
            count += 1
    return count


def compile_one(driver, target, source, asm):
    command = ([str(driver), "--target=" + target] + list(TARGET_OPTIONS.get(target, ())) +
               ["-S", str(source), "-o", str(asm)])
    pre = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter_ns()
    result = subprocess.run(
        command,
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
        "assembly_opcode_lines_estimate": asm_opcode_lines(asm),
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
    cli.add_argument("--label", default="tinyAES-128-ECB")
    cli.add_argument("--compile-only", action="store_true",
                     help="Assembly-level regression only; source has no main()")
    cli.add_argument("--five-isa", action="store_true",
                     help="Include RISC-V RV32I/ILP32 in addition to the original four targets")
    args = cli.parse_args()
    targets = FIVE_TARGETS if args.five_isa else TARGETS
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
        "# MiniC " + str(len(targets)) + "-ISA original " + args.label + " source: paired refactor A/B",
        "",
        "Baseline and candidate are separate compiler builds. The exact same "
        "preprocessed original " + args.label + " C source is supplied to both.",
        "",
        "Input SHA-256: `" + sha256(source) + "`",
        "",
        "| Target | Baseline wall p50 (ms) | Candidate wall p50 (ms) | "
        "Speedup | Assembly bytes before → after | Size reduction | "
        "Opcode-line estimate before → after |",
        "| :-- | --: | --: | --: | --: | --: | --: |",
    ]
    for target in targets:
        tdir = args.output / target
        tdir.mkdir(parents=True, exist_ok=True)
        samples = []
        first_asm = {}
        warmups = {}
        for phase in ("before", "after"):
            asm = tdir / (phase + "-warm.s")
            first_asm[phase] = asm
            warmups[phase] = compile_one(compilers[phase], target, source, asm)
        for iteration in range(args.repeats):
            order = ("before", "after") if iteration % 2 == 0 else ("after", "before")
            for phase in order:
                record = compile_one(
                    compilers[phase], target, source,
                    tdir / (phase + "-" + str(iteration) + ".s"),
                )
                if record["assembly_sha256"] != warmups[phase]["assembly_sha256"]:
                    raise RuntimeError("Nondeterministic assembly for " + phase +
                                       " target=" + target + " iteration=" + str(iteration))
                samples.append({"iteration": iteration, "phase": phase, **record})
        baseline_wall = statistics.median(
            r["wall_ms"] for r in samples if r["phase"] == "before")
        candidate_wall = statistics.median(
            r["wall_ms"] for r in samples if r["phase"] == "after")
        before_sha = sha256(first_asm["before"])
        after_sha = sha256(first_asm["after"])
        identical = before_sha == after_sha
        before_bytes = warmups["before"]["assembly_bytes"]
        after_bytes = warmups["after"]["assembly_bytes"]
        before_ops = warmups["before"]["assembly_opcode_lines_estimate"]
        after_ops = warmups["after"]["assembly_opcode_lines_estimate"]
        bytes_reduced_pct = 100 * (before_bytes - after_bytes) / before_bytes
        entry = {
            "target": target,
            "source_sha256": sha256(source),
            "baseline_p50_ms": baseline_wall,
            "candidate_p50_ms": candidate_wall,
            "speedup": round(baseline_wall / candidate_wall, 4),
            "baseline_assembly_sha256": before_sha,
            "candidate_assembly_sha256": after_sha,
            "assembly_identical": identical,
            "baseline_assembly_bytes": before_bytes,
            "candidate_assembly_bytes": after_bytes,
            "assembly_bytes_reduction_pct": round(bytes_reduced_pct, 4),
            "baseline_assembly_opcode_lines_estimate": before_ops,
            "candidate_assembly_opcode_lines_estimate": after_ops,
            "assembly_opcode_lines_reduced": before_ops - after_ops,
            "compiler_target_flags": list(TARGET_OPTIONS.get(target, ())),
            "samples": samples,
        }
        if target == "x86_64-linux":
            if args.compile_only:
                # cJSON provides an object, not a main(). Assemble both and
                # require native assembler acceptance instead of inventing
                # executable runtime semantics for a library TU.
                for phase, path in first_asm.items():
                    subprocess.run(
                        ["cc", "-c", "-x", "assembler", str(path),
                         "-o", str(tdir / (phase + ".o"))],
                        check=True, capture_output=True, text=True, timeout=60,
                    )
                entry["x86_native_assemble"] = "pass"
            else:
                native = {
                    phase: native_observable(path, tdir / (phase + ".exe"))
                    for phase, path in first_asm.items()
                }
                if native["before"] != native["after"] or native["before"]["returncode"] != 0:
                    raise RuntimeError("x86-64 original " + args.label +
                                       " runtime differential: " + str(native))
                entry["x86_native_runtime"] = native
        outcomes.append(entry)
        rows.append(
            f"| {target} | {baseline_wall:.3f} | {candidate_wall:.3f} | "
            f"{baseline_wall/candidate_wall:.4f}× | {before_bytes} → {after_bytes} | "
            f"{bytes_reduced_pct:+.2f}% | {before_ops} → {after_ops} |"
        )
        print(
            f"MINIC_REFACTOR_AB target={target} baseline_ms={baseline_wall:.3f} "
            f"candidate_ms={candidate_wall:.3f} speedup={baseline_wall/candidate_wall:.4f} "
            f"assembly_identical={identical} asm_before={before_bytes} "
            f"asm_after={after_bytes} opcode_lines_before={before_ops} "
            f"opcode_lines_after={after_ops}", flush=True
        )

    data = {
        "schema": "minic-isa-refactor-paired-v3" if args.five_isa else
                  "minic-four-isa-refactor-paired-v2",
        "source_label": args.label,
        "compile_only": args.compile_only,
        "scope": "one original C source, " + str(len(targets)) +
                 " ISAs, serial ABBA, not full Linux",
        "targets": list(targets),
        "compiler_sha256": identities,
        "source_sha256": sha256(source),
        "repeats_per_compiler_per_isa": args.repeats,
        "warmups_per_compiler_per_isa": 1,
        "results": outcomes,
    }
    (args.output / "results.json").write_text(json.dumps(data, indent=2) + "\n")
    rows += [
        "",
        ("Native x86-64 pre/post assembly passes the GNU assembler; "
         "this cJSON translation unit does not contain main()."
         if args.compile_only else
         "Native x86-64 baseline/candidate AES runtime exit=0 and stdout/stderr "
         "SHA-256 identical.") +
        " Non-x86 outputs are compile-level A/B only: "
        "GNU/QEMU runtime is covered by a separate focused suite.",
        "",
        "Interpretation: wall time is susceptible to hosted-runner contention; "
        "CPU timing samples and deterministic per-ISA assembly hashes are retained "
        "in results.json. Opcode-line counts are assembly-text estimates, not "
        "machine instruction counts or execution throughput. Two isolated "
        "translation units are **not** a representative Linux throughput claim.",
    ]
    (args.output / "summary.md").write_text("\n".join(rows) + "\n")
    print("MINIC_REFACTOR_PAIRED_AB=PASS source=" + args.label +
          " targets=" + str(len(targets)) + " x86_native=" +
          ("assembly_accepted" if args.compile_only else "runtime_equal"),
          flush=True)


if __name__ == "__main__":
    main()
