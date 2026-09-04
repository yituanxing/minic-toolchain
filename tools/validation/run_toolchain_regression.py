#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]

CORE_GATES = [
    ("build", "release-build", ["all"]),
    ("compiler", "check-fast", ["check-fast"]),
    ("preprocessor", "minipp-a0", ["check-minipp-a0"]),
    ("assembler", "minias-a0", ["check-minias-a0"]),
    ("elf", "minielf-reader-a0", ["check-minielf-reader-a0"]),
    ("archiver", "miniar-a0", ["check-miniar-a0"]),
    ("linker", "minild-a3", ["check-minild-a3"]),
]

FULL_EXTRA_GATES = [
    ("archiver", "miniar-a1", ["check-miniar-a1"]),
    ("object-tools", "mininm-a0", ["check-mininm-a0"]),
    ("object-tools", "mininm-a1", ["check-mininm-a1"]),
    ("object-tools", "miniobjcopy-a0", ["check-miniobjcopy-a0"]),
    ("object-tools", "miniobjcopy-a1", ["check-miniobjcopy-a1"]),
    ("object-tools", "minicstrip-a0", ["check-minicstrip-a0"]),
    ("linker", "minild-a0", ["check-minild-a0"]),
    ("linker", "minild-a1", ["check-minild-a1"]),
    ("linker", "minild-a2", ["check-minild-a2"]),
    ("linker", "minild-a4", ["check-minild-a4"]),
    ("linker", "minild-a5", ["check-minild-a5"]),
    ("linker", "minild-a6", ["check-minild-a6"]),
    ("linker", "minild-script-a0", ["check-minild-script-a0"]),
    ("linker", "minild-script-a1", ["check-minild-script-a1"]),
]

FAILURE_HINT = {
    "build": "build-system",
    "compiler": "compiler",
    "preprocessor": "preprocessor",
    "assembler": "assembler",
    "elf": "object-format",
    "archiver": "archiver",
    "object-tools": "object-tools",
    "linker": "linker",
    "runtime": "runtime",
    "project": "project-closure",
}

def run_gate(category, name, targets, common_make, timeout_s):
    command = ["make", *common_make, *targets]
    start = time.monotonic()
    try:
        proc = subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_s,
            check=False,
        )
        status = proc.returncode
        stdout = proc.stdout
        stderr = proc.stderr
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        status = 124
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        timed_out = True

    seconds = round(time.monotonic() - start, 3)
    ok = status == 0
    return {
        "category": category,
        "name": name,
        "ok": ok,
        "status": status,
        "seconds": seconds,
        "timed_out": timed_out,
        "failure_class_hint": None if ok else FAILURE_HINT.get(category, "unknown"),
        "command": command,
        "stdout_tail": stdout[-4000:],
        "stderr_tail": stderr[-4000:],
    }

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=("core", "full"), default="core")
    parser.add_argument(
        "--build-dir",
        default="build/toolchain-regression-v0",
    )
    parser.add_argument(
        "--json-out",
        default="build/toolchain-regression-v0.json",
    )
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args()

    gates = list(CORE_GATES)
    if args.profile == "full":
        gates.extend(FULL_EXTRA_GATES)

    common_make = [
        "-j4",
        "MODE=release",
        "CFLAGS=-Werror",
        f"BUILD_DIR={args.build_dir}",
        f"RISCV_CC={os.environ.get('RISCV_CC', 'riscv64-linux-gnu-gcc')}",
        f"QEMU_RISCV64={os.environ.get('QEMU_RISCV64', 'qemu-riscv64')}",
    ]

    results = []
    for category, name, targets in gates:
        print(f"[toolchain-regression] START {category}::{name}", flush=True)
        result = run_gate(category, name, targets, common_make, args.timeout)
        results.append(result)
        state = "PASS" if result["ok"] else "FAIL"
        print(
            f"[toolchain-regression] {state} {category}::{name} "
            f"status={result['status']} seconds={result['seconds']}",
            flush=True,
        )

    summary = {
        "schema": 1,
        "goal": "C toolchain reusable regression ledger",
        "profile": args.profile,
        "timestamp_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "total": len(results),
        "pass": sum(1 for item in results if item["ok"]),
        "fail": sum(1 for item in results if not item["ok"]),
        "results": results,
    }

    output = ROOT / args.json_out
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    print(
        f"TOOLCHAIN_REGRESSION_V0=PASS {summary['pass']}/{summary['total']}"
        if summary["fail"] == 0
        else f"TOOLCHAIN_REGRESSION_V0=FAIL {summary['pass']}/{summary['total']}",
        flush=True,
    )
    return 0 if summary["fail"] == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
