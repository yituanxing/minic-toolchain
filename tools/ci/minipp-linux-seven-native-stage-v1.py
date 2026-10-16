#!/usr/bin/env python3
"""Seven independent real-Linux PP -> MiniC -> MiniAS provenance probes.

This is a staged PP integration gate, NOT full-Kbuild or QEMU acceptance.
Each of the seven existing gcc-shards runners selects actual Kbuild C
objects from the SHA-attested GNU golden pool. GNU is used only as the
reference for macro context and exact preprocessed bytes. The compiler
takes MiniPP-produced .i, never the GNU reference .i. No golden object is
overwritten; failure samples and ELF attestations are retained per shard.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests/preprocessor"))
from run_linux_native_full_production import extract, predef_args  # noqa: E402


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pp_args(args: list[str]) -> list[str]:
    """Match existing audited linux_kbuild_pp_cc.sh argument normalization."""
    out: list[str] = []
    takes_value = {"-D", "-U", "-I", "-isystem", "-include"}
    i = 0
    while i < len(args):
        a = args[i]
        if a in takes_value:
            if i + 1 >= len(args):
                raise ValueError(f"truncated Kbuild option: {a}")
            out.extend((a, args[i + 1]))
            i += 2
            continue
        if a.startswith(("-D", "-U", "-I", "-isystem", "-include")) or a == "-nostdinc":
            out.append(a)
        i += 1
    return out


def run(cmd: list[str], cwd: Path, output: Path,
        env: dict[str, str] | None = None) -> tuple[int, int]:
    """Actual child wall time and per-process peak RSS from Linux wait4.

    Avoid a second /usr/bin/time wrapper process, false peak-RSS estimates,
    and reading potentially large stderr/stdout into process RAM.
    """
    started_ns = time.monotonic_ns()
    with tempfile.TemporaryFile(mode="w+b") as stdout, \
         tempfile.TemporaryFile(mode="w+b") as stderr:
        p = subprocess.Popen(cmd, cwd=cwd, stdout=stdout, stderr=stderr, env=env)
        pid, status, usage = os.wait4(p.pid, 0)
        assert pid == p.pid
        p.returncode = os.waitstatus_to_exitcode(status)
        elapsed_ms = (time.monotonic_ns() - started_ns) // 1_000_000
        maxrss_kb = usage.ru_maxrss
        def last_text(stream, bound: int) -> str:
            stream.seek(0, 2)
            stream.seek(max(0, stream.tell() - bound))
            return stream.read().decode(errors="replace")
        output.write_text("COMMAND " + json.dumps(cmd) + "\n"
                          + f"returncode={p.returncode}\nelapsed_ms={elapsed_ms}\n"
                          + f"maxrss_kb={maxrss_kb}\n"
                          + f"user_cpu_ms={round(usage.ru_utime * 1000)}\n"
                          + f"system_cpu_ms={round(usage.ru_stime * 1000)}\n"
                          + f"stdout:\n{last_text(stdout, 4000)}"
                          + f"\nstderr:\n{last_text(stderr, 11000)}\n")
    if p.returncode:
        raise RuntimeError(f"returncode={p.returncode}: {cmd[0]}")
    return elapsed_ms, maxrss_kb


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--src", type=Path, required=True)
    p.add_argument("--owner-list", type=Path, required=True)
    p.add_argument("--stage", type=Path, required=True)
    p.add_argument("--minipp", type=Path, required=True)
    p.add_argument("--minic-cc", type=Path, required=True)
    p.add_argument("--minias", type=Path, required=True)
    p.add_argument("--shard", type=int, required=True)
    p.add_argument("--shards", type=int, required=True)
    p.add_argument("--sample-per-shard", type=int, default=2)
    p.add_argument("--profile-snapshot", type=Path)
    p.add_argument("--native-macro-rules", action="store_true")
    args = p.parse_args()
    if args.profile_snapshot is not None and not args.profile_snapshot.is_file():
        raise SystemExit("MINIPP_SEVEN=FAIL native_macro_snapshot_missing")
    if args.shards != 7 or not 0 <= args.shard < 7:
        raise SystemExit("MINIPP_SEVEN=FAIL invalid-seven-runner-partition")
    if not 0 <= args.sample_per_shard <= 512:
        raise SystemExit("MINIPP_SEVEN=FAIL invalid-sample-limit")
    out, src, stage = args.out.resolve(), args.src.resolve(), args.stage.resolve()
    stage.mkdir(parents=True, exist_ok=True)
    owner_names = [s.strip() for s in args.owner_list.read_text().splitlines()
                   if s.strip() and not s.lstrip().startswith("#")]
    if not 1900 <= len(owner_names) <= 3000 or len(set(owner_names)) != len(owner_names):
        raise SystemExit(f"MINIPP_SEVEN=FAIL invalid-real-owner-universe:{len(owner_names)}")
    entries = {row["object"]: row for row in extract(out, src)}
    missing = [name for name in owner_names if name not in entries]
    if missing:
        raise SystemExit("MINIPP_SEVEN=FAIL missing-real-Kbuild-C-producers:"
                         + ",".join(missing[:8]))
    assigned = owner_names[args.shard::args.shards]
    selected = (assigned if args.sample_per_shard == 0
                else assigned[:args.sample_per_shard])
    if (not selected or
        (args.sample_per_shard != 0 and len(selected) != args.sample_per_shard)):
        raise SystemExit("MINIPP_SEVEN=FAIL incomplete-worker-selection")
    print(f"MINIPP_SEVEN_PARTITION=PASS worker={args.shard}/7 "
          f"frozen_owners={len(owner_names)} selected={len(selected)} "
          "compiler_input=MiniPP.i GNU_reference_only=true", flush=True)
    if args.profile_snapshot is not None:
        print(f"MINIPP_SEVEN_PINNED_PROFILE=ENABLED worker={args.shard}/7 "
              "GCC_predefine_processes=0 frozen_target_profiles=true", flush=True)
    rows = []
    predefs: dict[tuple[str, ...], Path] = {}
    for object_name in selected:
        row = entries[object_name]
        key = hashlib.sha256(object_name.encode()).hexdigest()[:16]
        item = stage / key
        item.mkdir(exist_ok=True)
        old_sha = sha(out / object_name)
        result = dict(object=object_name, source=row["source"], status="FAIL",
                      frozen_original_sha=old_sha, timings_ms={},
                      peak_rss_kb={})
        rows.append(result)
        try:
            context = predef_args(row["args"])
            if context not in predefs:
                macro_path = stage / ("predefs-" +
                    hashlib.sha256(json.dumps(context).encode()).hexdigest()[:16] + ".h")
                env = os.environ.copy()
                env.update(REAL_CC="riscv64-linux-gnu-gcc",
                           MINIPP_LINUX_SRC=str(src),
                           MINIPP_PREDEFINES_OUT=str(macro_path))
                if args.profile_snapshot is not None:
                    env["MINIPP_PREDEFINES_SNAPSHOT"] = str(
                        args.profile_snapshot.resolve())
                if args.native_macro_rules:
                    env["MINIPP_PREDEFINES_NATIVE_RULES"] = "1"
                started_ns = time.monotonic_ns()
                proc = subprocess.run(
                    ["bash", "tests/preprocessor/build_reference_predefines.sh",
                     *context], cwd=Path(__file__).resolve().parents[2],
                    env=env, capture_output=True, text=True)
                result["timings_ms"]["macro_predefines"] = (
                    time.monotonic_ns() - started_ns) // 1_000_000
                (item / "predefines.log").write_text(proc.stdout + "\n" + proc.stderr)
                if proc.returncode or not macro_path.is_file():
                    raise RuntimeError("reference-GCC-macro-context-unavailable")
                predefs[context] = macro_path
            macro_path = predefs[context]
            if "-mcmodel=medany" in row["args"] and b"__riscv_cmodel_medany" not in macro_path.read_bytes():
                raise RuntimeError("medany-specific-GCC-predefine-context-not-preserved")
            common = ["-E", "-P", "-undef", "-x", "c", "-include", str(macro_path),
                      *pp_args(row["args"]), row["source"]]
            reference = item / "gnu.i"
            mini = item / "mini.i"
            result["timings_ms"]["gnu_pp_oracle"], result["peak_rss_kb"]["gnu_pp_oracle"] = run(
                ["riscv64-linux-gnu-gcc", *common, "-o", str(reference)],
                out, item / "reference.log")
            result["timings_ms"]["minipp"], result["peak_rss_kb"]["minipp"] = run(
                [str(args.minipp.resolve()), *common, "-o", str(mini)],
                out, item / "minipp.log")
            result["gnu_pp_sha"] = sha(reference)
            result["mini_pp_sha"] = sha(mini)
            if reference.read_bytes() != mini.read_bytes():
                # Fail closed and retain bounded first-difference context.
                import difflib
                diff = list(difflib.unified_diff(
                    reference.read_text(errors="replace").splitlines(),
                    mini.read_text(errors="replace").splitlines(),
                    fromfile="GNU", tofile="MiniPP", n=2))
                (item / "mismatch.diff").write_text("\n".join(diff[:100]) + "\n")
                raise RuntimeError("MiniPP-and-GNU-preprocessed-bytes-differ")
            asm = item / "minic.s"
            obj = item / "minias.o"
            # Profile coarse native phases without recompiling any translation
            # unit twice. The ordinary compiler output path is unchanged.
            native_env = dict(os.environ)
            native_env["MINIC_PROFILE_PHASES"] = "1"
            result["timings_ms"]["minic"], result["peak_rss_kb"]["minic"] = run(
                [str(args.minic_cc.resolve()), "-S", str(mini), "-o", str(asm)],
                out, item / "minic.log", env=native_env)
            result["compiler_phase_ms"] = {}
            result["compiler_phase_highwater_rss_kb"] = {}
            for line in (item / "minic.log").read_text(errors="replace").splitlines():
                match = re.search(
                    r"MINIC_PHASE stage=(\S+) elapsed_ms=(\d+) "
                    r"process_highwater_rss_kb=(-?\d+)", line)
                if match:
                    phase, millis, rss = match.groups()
                    result["compiler_phase_ms"][phase] = int(millis)
                    result["compiler_phase_highwater_rss_kb"][phase] = int(rss)
                total = re.search(r"MINIC_PROFILE_TOTAL elapsed_ms=(\d+)", line)
                if total:
                    result["compiler_profile_total_ms"] = int(total.group(1))
            march = next((x for x in row["args"] if x.startswith("-march=")),
                         "-march=rv64imac")
            abi = next((x for x in row["args"] if x.startswith("-mabi=")),
                       "-mabi=lp64")
            result["timings_ms"]["minias"], result["peak_rss_kb"]["minias"] = run(
                [str(args.minias.resolve()), march, abi, "-o", str(obj), str(asm)],
                out, item / "minias.log")
            elf = obj.read_bytes()
            golden = (out / object_name).read_bytes()
            if not (len(elf) >= 52 and elf[:6] == b"\x7fELF\x02\x01"
                    and int.from_bytes(elf[16:18], "little") == 1
                    and int.from_bytes(elf[18:20], "little") == 243
                    and elf[48:52] == golden[48:52]):
                raise RuntimeError("MiniPP-MiniC-MiniAS-ELF64-RISC-V-ABI-mismatch")
            if sha(out / object_name) != old_sha:
                raise RuntimeError("golden-cached-object-overwritten")
            # Full-mode native .o files are hard-linked into their exact
            # relative Kbuild paths for a later hash-attested seven-way
            # merge. Never mutate the frozen GNU golden .o tree.
            artifact_object = stage / "native-objects" / object_name
            artifact_object.parent.mkdir(parents=True, exist_ok=True)
            os.link(obj, artifact_object)
            result.update(status="PASS", native_object_sha=sha(obj),
                          native_object_bytes=obj.stat().st_size,
                          minipp_input_bytes=mini.stat().st_size,
                          minic_assembly_bytes=asm.stat().st_size)
            print(f"MINIPP_SEVEN_NATIVE_OBJECT=PASS worker={args.shard}/7 "
                  f"object={object_name} pp=MiniPP cc=MiniC as=MiniAS "
                  "GNU_pp_consumed_by_MiniC=false", flush=True)
        except Exception as exc:
            result["error"] = str(exc)
            print(f"MINIPP_SEVEN_NATIVE_OBJECT=FAIL worker={args.shard}/7 "
                  f"object={object_name} reason={exc}", flush=True)
            if args.sample_per_shard == 0:
                # Full qualification is all-or-nothing. Do not spend minutes
                # processing the other ~294 objects on a worker after its
                # first reproducible failed owner. Preserve this owner's
                # logs/identities and surface the earliest failure immediately.
                (stage / "first-failure.json").write_text(
                    json.dumps(result, indent=2, sort_keys=True) + "\n")
                print(f"MINIPP_SEVEN_FAIL_FAST=TRIGGERED "
                      f"worker={args.shard}/7 object={object_name} "
                      "full_mode=true retained_first_failure=true",
                      flush=True)
                break
    # Stage-by-stage wall time and produced bytes are additive across owners;
    # exact per-child peak RSS is measured using Linux wait4 and aggregated as
    # maximum, not an invalid sum of per-process memory peaks. This is NOT
    # the concurrent job/cgroup memory peak.
    profile_stages = ("macro_predefines", "gnu_pp_oracle",
                      "minipp", "minic", "minias")
    stage_totals_ms = {
        name: sum(row.get("timings_ms", {}).get(name, 0) for row in rows)
        for name in profile_stages
    }
    compiler_phase_counts = {}
    compiler_phase_ms_sum = {}
    compiler_phase_process_highwater_rss_kb = {}
    for row in rows:
        for phase, elapsed in row.get("compiler_phase_ms", {}).items():
            compiler_phase_counts[phase] = compiler_phase_counts.get(phase, 0) + 1
            compiler_phase_ms_sum[phase] = (
                compiler_phase_ms_sum.get(phase, 0) + elapsed)
            rss = row.get("compiler_phase_highwater_rss_kb", {}).get(phase, 0)
            compiler_phase_process_highwater_rss_kb[phase] = max(
                compiler_phase_process_highwater_rss_kb.get(phase, 0), rss)
    stage_peak_rss_kb = {
        name: max((row.get("peak_rss_kb", {}).get(name, 0) for row in rows),
                  default=0)
        for name in ("gnu_pp_oracle", "minipp", "minic", "minias")
    }
    (stage / "performance-summary.json").write_text(json.dumps({
        "worker": args.shard, "selected": len(rows),
        "stage_wall_ms_sum": stage_totals_ms,
        "compiler_phase_counts": compiler_phase_counts,
        "compiler_phase_ms_sum": compiler_phase_ms_sum,
        "compiler_phase_process_highwater_rss_kb":
            compiler_phase_process_highwater_rss_kb,
        "stage_max_child_rss_kb": stage_peak_rss_kb,
        "minipp_output_bytes_sum": sum(row.get("minipp_input_bytes", 0)
                                       for row in rows),
        "minic_assembly_bytes_sum": sum(row.get("minic_assembly_bytes", 0)
                                        for row in rows),
        "native_object_bytes_sum": sum(row.get("native_object_bytes", 0)
                                       for row in rows),
        "measurement_scope": "actual per-command wall times, not full runner time",
        "memory_scope": "max individual process RSS, not concurrent cgroup peak",
    }, indent=2, sort_keys=True) + "\n")
    slowest = sorted((row for row in rows if row["status"] == "PASS"),
                     key=lambda row: row["timings_ms"].get("minic", 0),
                     reverse=True)[:5]
    for row in slowest:
        print(f"MINIPP_SEVEN_SLOWEST_MINIC worker={args.shard}/7 "
              f"elapsed_ms={row['timings_ms'].get('minic', 0)} "
              f"peak_rss_kb={row['peak_rss_kb'].get('minic', 0)} "
              f"input_i_bytes={row.get('minipp_input_bytes', 0)} "
              f"object={row['object']}", flush=True)
    coverage = sum("compiler_profile_total_ms" in row for row in rows)
    print(f"MINIPP_SEVEN_COMPILER_PHASE_COVERAGE worker={args.shard}/7 "
          f"measured={coverage}/{len(rows)} "
          "missing_is_explicit=true no_extra_compilation=true", flush=True)
    print("MINIPP_SEVEN_COMPILER_PHASES worker=" + str(args.shard) +
          "/7 " + " ".join(f"{phase}_ms={elapsed}"
                           for phase, elapsed in sorted(compiler_phase_ms_sum.items()))
          + " memory=per_stage_process_highwater_not_allocated_bytes", flush=True)
    print(f"MINIPP_SEVEN_PERFORMANCE worker={args.shard}/7 "
          + " ".join(f"{stage}_ms={total}" for stage, total in stage_totals_ms.items())
          + " " + " ".join(f"{stage}_maxrss_kb={rss}"
                          for stage, rss in stage_peak_rss_kb.items())
          + " memory_scope=max_individual_child"
          + " source=monotonic_wall_and_linux_wait4", flush=True)
    (stage / "results.json").write_text(json.dumps(rows, indent=2, sort_keys=True) + "\n")
    (stage / "native-manifest.sha256").write_text("".join(
        f'{row["native_object_sha"]}  native-objects/{row["object"]}\n'
        for row in rows if row["status"] == "PASS"))
    passed = sum(x["status"] == "PASS" for x in rows)
    (stage / "summary.json").write_text(json.dumps({
        "shard": args.shard, "shards": 7, "selected": len(rows),
        "passed": passed, "failed": len(rows)-passed, "full_universe": len(owner_names),
        "status": "PASS" if passed == len(rows) else "FAIL",
        "qemu_claim": False, "full_2064_pp_claim": False,
        "full_shard": args.sample_per_shard == 0,
        "builtin_profile": ("native_rules" if args.native_macro_rules
                            else "pinned" if args.profile_snapshot is not None
                            else "gcc_reference"),
        "builtin_profile_sha": (sha(args.profile_snapshot.resolve())
                                if args.profile_snapshot is not None else None),
        "manifest_sha": sha(stage / "native-manifest.sha256"),
    }, indent=2, sort_keys=True) + "\n")
    print(f"MINIPP_SEVEN_NATIVE_RESULT={'PASS' if passed == len(rows) else 'FAIL'} "
          f"worker={args.shard}/7 passed={passed}/{len(rows)} "
          "full_Kbuild_claim=false QEMU_claim=false", flush=True)
    if args.profile_snapshot is not None and passed == len(rows):
        print(f"MINIPP_SEVEN_PINNED_PROFILE=PASS worker={args.shard}/7 "
              f"native_C={passed} no_GCC_target_macro_generation=true "
              "GNU_reference_only=true", flush=True)
    if args.native_macro_rules and passed == len(rows):
        print(f"MINIPP_SEVEN_NATIVE_MACRO_RULES=PASS worker={args.shard}/7 "
              f"native_C={passed} builtin_context=generated_by_Mini_rules "
              "GNU_reference_bytes_equal=true GCC_macro_processes=0",
              flush=True)
    return 0 if passed == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
