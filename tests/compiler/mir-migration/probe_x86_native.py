#!/usr/bin/env python3
"""Focused host x86-64 executable differential, before Linux-scale tests.

Build each fixture twice: GNU C directly, and MiniC GNU assembly linked by
the SAME host GCC. Compare process exit code and both output streams. Do not
claim that this proves four other ISAs or a Linux kernel.
"""

import argparse
from pathlib import Path
import subprocess
import sys

from probe_machine_ir import CASES, LEGACY_UNSUPPORTED, Failure, ROOT, demand


def run(args, timeout=15):
    try:
        proc = subprocess.run(args, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        raise Failure(f"timed out ({timeout}s): {args}") from error
    return proc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--minic-cc", type=Path, required=True)
    parser.add_argument("--host-cc", default="cc")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/debug")
    parser.add_argument("--case", action="append", choices=CASES,
                        help="single focused program instead of the full small-software matrix")
    args = parser.parse_args()
    binary = args.minic_cc.resolve(strict=True)
    work = args.build_dir.resolve() / "tests/mir-migration/native-x86"
    work.mkdir(parents=True, exist_ok=True)
    cases = args.case or CASES
    passes = 0
    for case in cases:
        if ("x86_64", case) in LEGACY_UNSUPPORTED:
            # Never call an unimplemented x86 source ASM a valid executable.
            print(f"X86_MACHINE_RUNTIME=LEGACY_UNSUPPORTED case={case}")
            continue
        source = Path(__file__).with_name(case + ".c")
        preprocessed = work / (case + ".i")
        assembler = work / (case + ".s")
        host_binary = work / ("host-" + case)
        minic_binary = work / ("minic-" + case)
        for path in (assembler, host_binary, minic_binary):
            path.unlink(missing_ok=True)
        steps = (
            ([args.host_cc, "-E", "-P", "-std=gnu11", "-x", "c",
              str(source), "-o", str(preprocessed)], "GNU preprocess"),
            ([str(binary), "--target=x86_64-linux", "-S",
              str(preprocessed), "-o", str(assembler)], "MiniC -S"),
            ([args.host_cc, "-std=gnu11", str(source), "-o", str(host_binary)],
             "GNU reference link"),
            ([args.host_cc, str(assembler), "-o", str(minic_binary)],
             "MiniC GNU assembly link"),
        )
        for command, stage in steps:
            proc = run(command, timeout=30)
            demand(proc.returncode == 0,
                   f"{case}: {stage} failed: {proc.stderr.decode(errors='replace')}")
        host = run([str(host_binary)])
        minic = run([str(minic_binary)])
        demand(host.returncode >= 0 and minic.returncode >= 0,
               f"{case}: process signal failure host={host.returncode} minic={minic.returncode}")
        demand((host.returncode, host.stdout, host.stderr) ==
               (minic.returncode, minic.stdout, minic.stderr),
               f"{case}: host/MiniC behavioral mismatch "
               f"exit={host.returncode}/{minic.returncode} "
               f"stdout={host.stdout!r}/{minic.stdout!r} "
               f"stderr={host.stderr!r}/{minic.stderr!r}")
        passes += 1
        print(f"X86_MACHINE_RUNTIME=PASS case={case} exit={minic.returncode}")
    demand(passes != 0, "no native x86 executable fixtures were tested")
    print(f"X86_MACHINE_RUNTIME=PASS total={passes}")


if __name__ == "__main__":
    try:
        main()
    except (Failure, OSError) as error:
        print(f"X86_MACHINE_RUNTIME=FAIL {error}", file=sys.stderr)
        sys.exit(1)
