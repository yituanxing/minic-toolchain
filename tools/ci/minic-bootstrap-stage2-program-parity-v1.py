#!/usr/bin/env python3
"""Check self-hosted Stage2 against *current Stage0* on every C0 real program.

Run the repository's full, unchanged tests/programs/c0/manifest.txt, not a
handpicked whitelist. Use the same GCC preprocess/CRT/static linker as the
existing run.sh. Require identical Stage0/Stage2 generated assembly (strict);
execute Stage2-produced RV64 ELF in QEMU. Independently run GNU reference and
report *every* observable mismatch without falsely calling it GCC-equivalent.

The original GCC differential suite is preserved and its existing
predefined_func_name GCC/MiniC pointer-identity disagreement is not hidden.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args, timeout=150, *, env=None):
    p = subprocess.run([str(x) for x in args], cwd=ROOT, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=timeout, check=False)
    return p.returncode, p.stdout, p.stderr


def require(args, *, env, timeout=150, label):
    try:
        rc, out, err = run(args, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        raise RuntimeError(label + " timed out") from None
    if rc:
        raise RuntimeError(label + " rc=" + str(rc) +
                           " stderr=" + err.decode(errors="replace")[-1900:])
    return rc, out, err


def names():
    manifest = ROOT / "tests/programs/c0/manifest.txt"
    all_names = []
    for line in manifest.read_text().splitlines():
        name = line.strip()
        if not name or name.startswith("#"):
            continue
        if not name.replace("_", "").isalnum() or not name.islower() or name in all_names:
            raise RuntimeError("invalid/duplicate program manifest entry " + name)
        all_names.append(name)
    actual = {p.stem for p in (ROOT / "tests/programs/c0").glob("*.c")}
    if set(all_names) != actual or not all_names:
        raise RuntimeError("C0 program manifest does not cover all sources")
    return all_names


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--stage0", type=Path, required=True)
    p.add_argument("--stage2-wrapper", type=Path, required=True)
    p.add_argument("--work", type=Path, required=True)
    opts = p.parse_args()
    env = dict(os.environ, MINIC_LINUX_FULL_CROSSBLOCK="1",
               MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ="1")
    outdir = opts.work.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    stage0 = opts.stage0.resolve()
    stage2 = opts.stage2_wrapper.resolve()
    if not stage0.is_file() or not stage2.is_file():
        raise RuntimeError("stage0/stage2 compiler executable missing")
    program_names = names()
    records = []
    print("BOOTSTRAP_STAGE2_PROGRAM_MANIFEST=PASS selected=" +
          str(len(program_names)), flush=True)

    def snapshot():
        (outdir / "program-parity.json").write_text(
            json.dumps({
                "stage0_sha256": sha(stage0), "program_count": len(program_names),
                "completed": len(records),
                "status": "IN_PROGRESS",
                "rows": records
            }, indent=2) + "\n")

    for number, name in enumerate(program_names, 1):
        source = ROOT / "tests/programs/c0" / (name + ".c")
        pre = outdir / (name + ".i")
        oldasm = outdir / (name + ".stage0.s")
        newasm = outdir / (name + ".stage2.s")
        stage2elf = outdir / (name + ".stage2.elf")
        gnuelf = outdir / (name + ".gnu.elf")
        require(["riscv64-linux-gnu-gcc", "-E", "-P", "-x", "c",
                 source, "-o", pre], env=env, label="preprocess " + name)
        if not pre.is_file() or pre.stat().st_size == 0:
            raise RuntimeError("empty GNU preprocessing " + name)
        require([stage0, "-S", pre, "-o", oldasm],
                env=env, label="Stage0 " + name)
        require([stage2, "-S", pre, "-o", newasm],
                env=env, timeout=180, label="Stage2 " + name)
        if not oldasm.is_file() or not newasm.is_file() or sha(oldasm) != sha(newasm):
            raise RuntimeError("BOOTSTRAP_STAGE2_PARITY=FAIL name=" +
                               name + " assembly SHA mismatch")
        require(["riscv64-linux-gnu-gcc", "-static", newasm, "-o", stage2elf],
                env=env, label="link Stage2 " + name)
        # Some C0 fixtures deliberately exercise an inline/linkage corner
        # which GNU -std=c11 -O0 cannot link (e.g. extern inline reference).
        # This is NOT a Stage2 compilation failure. Do not hide or drop it:
        # persist the GNU failure and continue checking Stage0/Stage2 parity
        # for all later manifest entries.
        gnu_link = run(["riscv64-linux-gnu-gcc", "-std=c11", "-O0", "-static",
                        source, "-o", gnuelf], env=env)
        try:
            stage2_result = run(["qemu-riscv64", stage2elf], env=env)
            gnu_result = run(["qemu-riscv64", gnuelf], env=env) if gnu_link[0] == 0 else None
        except subprocess.TimeoutExpired:
            raise RuntimeError("QEMU runtime timed out " + name) from None
        gcc_equal = gnu_result is not None and stage2_result == gnu_result
        row = {
            "program": name,
            "stage0_stage2_asm_equal": True,
            "stage0_stage2_asm_sha256": sha(newasm),
            "stage2_exit": stage2_result[0],
            "gnu_exit": gnu_result[0] if gnu_result is not None else None,
            "gnu_reference": "OK" if gnu_result is not None else "LINK_FAILED",
            "gnu_build_error": (
                gnu_link[2].decode(errors="replace")[-1500:]
                if gnu_result is None else None),
            "gnu_observable_equal": gcc_equal,
            "stage2_stdout_sha256": hashlib.sha256(stage2_result[1]).hexdigest(),
            "stage2_stderr_sha256": hashlib.sha256(stage2_result[2]).hexdigest(),
            "gnu_stdout_sha256": (
                hashlib.sha256(gnu_result[1]).hexdigest() if gnu_result is not None else None),
            "gnu_stderr_sha256": (
                hashlib.sha256(gnu_result[2]).hexdigest() if gnu_result is not None else None)
        }
        records.append(row)
        snapshot()
        print("BOOTSTRAP_STAGE2_PROGRAM_PARITY=PASS count=" + str(number) +
              "/" + str(len(program_names)) + " name=" + name +
              " asm=identical gcc=" +
              ("MATCH" if gcc_equal else ("DIFF" if gnu_result is not None else "LINK_FAILED")) +
              " minic_exit=" + str(stage2_result[0]) +
              " gcc_exit=" + str(gnu_result[0] if gnu_result is not None else "NA"),
              flush=True)

    gnu_mismatches = [r for r in records if r["gnu_reference"] == "OK"
                      and not r["gnu_observable_equal"]]
    gnu_build_failures = [r for r in records if r["gnu_reference"] == "LINK_FAILED"]
    report = {
        "status": "STAGE0_STAGE2_PARITY_PASS",
        "stage0_sha256": sha(stage0),
        "program_count": len(program_names),
        "completed": len(records),
        "stage0_stage2_asm_equal": len(records),
        "gnu_equal": len(records)-len(gnu_mismatches)-len(gnu_build_failures),
        "gnu_build_failures": [
            {"program": z["program"], "error": z["gnu_build_error"]}
            for z in gnu_build_failures],
        "gnu_mismatches": [
            {"program": z["program"], "minic_exit": z["stage2_exit"],
             "gcc_exit": z["gnu_exit"],
             "minic_stdout_sha": z["stage2_stdout_sha256"],
             "gnu_stdout_sha": z["gnu_stdout_sha256"],
             "minic_stderr_sha": z["stage2_stderr_sha256"],
             "gnu_stderr_sha": z["gnu_stderr_sha256"]}
            for z in gnu_mismatches],
        "rows": records
    }
    (outdir / "program-parity.json").write_text(
        json.dumps(report, indent=2) + "\n")
    print("BOOTSTRAP_STAGE2_PARITY=PASS assembly=" + str(len(records)) +
          "/" + str(len(records)) + " runtime_elf=" + str(len(records)) +
          " gcc_equal=" + str(len(records)-len(gnu_mismatches)-len(gnu_build_failures)) +
          " gcc_different=" + str(len(gnu_mismatches)) +
          " gcc_link_failed=" + str(len(gnu_build_failures)) +
          " scope=current_stage0_to_certified_stage2", flush=True)
    for record in gnu_mismatches:
        print("BOOTSTRAP_STAGE2_GNU_MISMATCH=OBSERVED name=" + record["program"] +
              " minic_exit=" + str(record["stage2_exit"]) +
              " gcc_exit=" + str(record["gnu_exit"]), flush=True)
    for record in gnu_build_failures:
        print("BOOTSTRAP_STAGE2_GNU_LINK_FAILURE=OBSERVED name=" + record["program"] +
              " error=" + record["gnu_build_error"].strip().replace("\n", " | "),
              flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError,RuntimeError) as e:
        print("BOOTSTRAP_STAGE2_PARITY=FAIL " + str(e), file=sys.stderr,flush=True)
        sys.exit(1)
