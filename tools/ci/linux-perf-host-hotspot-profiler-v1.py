#!/usr/bin/env python3
"""Profile the host MiniC compiler on a frozen Linux .i (NOT target QEMU runtime).

Prefer CPU-sampled perf if permitted; otherwise Callgrind records executed
instructions, call counts and source-line attribution. This is diagnostic
only and MUST NOT be interpreted as normal compile wall-time.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import time
from pathlib import Path


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as fd:
        for data in iter(lambda: fd.read(1 << 20), b""):
            h.update(data)
    return h.hexdigest()


def run(command, timeout, logfile):
    started = time.monotonic()
    try:
        with logfile.open("wb") as output:
            proc = subprocess.run(command, stdout=output,
                                  stderr=subprocess.STDOUT, timeout=timeout,
                                  check=False)
        return proc.returncode == 0, time.monotonic() - started
    except subprocess.TimeoutExpired:
        with logfile.open("ab") as output:
            output.write(b"\nPROFILER_TIMEOUT\n")
        return False, time.monotonic() - started


def main():
    ap = argparse.ArgumentParser()
    for key in ("input", "debug-compiler", "release-compiler", "out"):
        ap.add_argument("--" + key, type=Path, required=True)
    ap.add_argument("--timeout", type=int, default=900)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for path in (args.input, args.debug_compiler, args.release_compiler):
        if not path.is_file():
            raise RuntimeError("Missing profiler file: " + str(path))
    expected = args.out / "release.s"
    ok, reference_time = run([str(args.release_compiler), "-S", str(args.input),
                              "-o", str(expected)], args.timeout,
                             args.out / "release.log")
    if not ok or not expected.is_file():
        raise RuntimeError("Release reference compiler failed")
    method = None
    instrumented_seconds = None
    if shutil.which("perf"):
        permitted, _ = run(["perf", "record", "-q", "-o",
                            str(args.out / "capability.data"), "--", "true"],
                           15, args.out / "perf-probe.log")
        if permitted:
            target = args.out / "perf.s"
            ok, instrumented_seconds = run(
                ["perf", "record", "-q", "-F", "99", "--call-graph", "dwarf",
                 "-o", str(args.out / "perf.data"), "--",
                 str(args.debug_compiler), "-S", str(args.input), "-o", str(target)],
                args.timeout, args.out / "perf-run.log")
            if ok and target.is_file() and digest(target) == digest(expected):
                ok_report, _ = run(["perf", "report", "--stdio", "-n",
                                    "-i", str(args.out / "perf.data")],
                                   90, args.out / "perf-top.txt")
                if not ok_report:
                    raise RuntimeError("perf sampled but report failed")
                method = "perf-sampled-cpu"
            elif ok:
                raise RuntimeError("perf compiler assembly differs from release")
    if method is None:
        if not shutil.which("valgrind") or not shutil.which("callgrind_annotate"):
            raise RuntimeError("No usable perf sampling or Callgrind installed")
        target = args.out / "callgrind.s"
        ok, instrumented_seconds = run(
            ["valgrind", "--tool=callgrind",
             "--callgrind-out-file=" + str(args.out / "callgrind.out"),
             "--log-file=" + str(args.out / "valgrind.log"),
             str(args.debug_compiler), "-S", str(args.input), "-o", str(target)],
            args.timeout, args.out / "callgrind-run.log")
        if not ok or not target.is_file():
            raise RuntimeError("Callgrind timed out or failed on frozen .i")
        if digest(target) != digest(expected):
            raise RuntimeError("instrumented .s differs from release .s")
        for fname, opts in (
            ("callgrind-self.txt", ["--inclusive=no", "--show=Ir"]),
            ("callgrind-inclusive.txt",
             ["--inclusive=yes", "--tree=both", "--show=Ir"])
        ):
            ok_ann, _ = run(["callgrind_annotate", "--auto=yes", *opts,
                             str(args.out / "callgrind.out")],
                            90, args.out / fname)
            if not ok_ann:
                raise RuntimeError("Callgrind annotation failed: " + fname)
        method = "callgrind-instructions-calls"
    result = {
        "schema": "minic-host-profiler-v1",
        "scope": "minic-cc host compilation of frozen .i; NOT GNU -E or QEMU",
        "input_sha256": digest(args.input),
        "release_cc_sha256": digest(args.release_compiler),
        "debug_cc_sha256": digest(args.debug_compiler),
        "assembly_equal": True, "assembly_sha256": digest(expected),
        "method": method,
        "reference_seconds": round(reference_time, 3),
        "instrumented_seconds": round(instrumented_seconds, 3),
        "warning": "Instrumented time is NOT normal-speed compiler A/B data."
    }
    (args.out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print("MINIC_HOTSPOT_PROFILE=PASS method=" + method
          + " input_sha256=" + result["input_sha256"]
          + " assembly_equal=true", flush=True)


if __name__ == "__main__":
    main()
