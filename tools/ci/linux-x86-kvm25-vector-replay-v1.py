#!/usr/bin/env python3
"""Fail-closed replay of the 25 *original* x86 KVM GNU .i owners.

Only owners previously recorded as MiniC failures in the frozen 2939-object
P2 audit are selected. This script verifies both .i and .o.cmd digests before
calling MiniC -S then the real GNU assembler, without GNU-C fallback.
It does NOT claim Linux KVM/VMX host capability or P2 runtime certification.
"""

import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import time


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def first_error(stderr):
    lines = stderr.splitlines()
    for line in lines:
        if "error:" in line or "Error:" in line or "Error " in line:
            return line[:500]
    return ""


def call(command, timeout):
    start = time.monotonic()
    try:
        process = subprocess.run(command, text=True, capture_output=True,
                                 timeout=timeout, check=False)
        return process.returncode, process.stdout, process.stderr, round(time.monotonic() - start, 3)
    except subprocess.TimeoutExpired as exc:
        return 124, "", f"TIMEOUT after {timeout}s: {exc}", round(time.monotonic() - start, 3)


def one(row, args):
    obj = row["object"]
    source = pathlib.Path(args.frozen) / row["input"]
    out_base = pathlib.Path(args.output) / obj
    asm = out_base.with_suffix(".s")
    elf_obj = out_base.with_suffix(".o")
    cmd = pathlib.Path(args.gnu_build) / obj
    cmd = cmd.parent / ("." + cmd.stem + ".o.cmd")
    result = {"object": obj, "input": str(source), "input_expected_sha256": row["sha256"],
              "cmd": str(cmd), "cmd_expected_sha256": row["cmd_sha256"],
              "stage": "BLOCKED", "minic_rc": None, "gnu_as_rc": None}
    for key, path, expected in (("input", source, row["sha256"]),
                                ("cmd", cmd, row["cmd_sha256"])):
        if not path.is_file():
            result["error"] = f"missing original frozen {key}: {path}"
            return result
        actual = digest(path)
        result[key + "_actual_sha256"] = actual
        if actual != expected:
            result["error"] = f"SHA mismatch {key}: {actual} != {expected}"
            return result
    asm.parent.mkdir(parents=True, exist_ok=True)
    # The GNU command records -fno-PIE; MiniC's documented canonical spelling
    # is -fno-pie. Both disable PIE, so normalize the spelling, not the policy.
    minic_flags = ["-fno-pie" if flag == "-fno-PIE" else flag
                   for flag in row["minic_flags"]]
    minic_command = [args.minic, "-S", str(source), "--target=x86_64-linux",
                     *minic_flags, "-o", str(asm)]
    minic_rc, _, minic_err, minic_time = call(minic_command, args.timeout)
    result.update(minic_rc=minic_rc, minic_seconds=minic_time,
                  minic_error=first_error(minic_err))
    if minic_rc != 0 or not asm.is_file() or asm.stat().st_size == 0:
        result["stage"] = "MINIC_S_FAIL"
        return result
    result["asm_sha256"] = digest(asm)
    gnu_as_command = [args.gnu_as, *row["gnu_as_flags"], "-c", "-x", "assembler",
                      str(asm), "-o", str(elf_obj)]
    as_rc, _, as_err, as_time = call(gnu_as_command, args.timeout)
    result.update(gnu_as_rc=as_rc, gnu_as_seconds=as_time,
                  gnu_as_error=first_error(as_err))
    if as_rc != 0 or not elf_obj.is_file() or elf_obj.stat().st_size == 0:
        result["stage"] = "GNU_AS_FAIL"
        return result
    result["object_sha256"] = digest(elf_obj)
    result["stage"] = "MINIC_S_GNU_AS_PASS"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frozen", default="/tmp/minic-p2-x86-gnu-frozen")
    parser.add_argument("--gnu-build", default="/tmp/minic-p2-audit-v1b-x86_64")
    parser.add_argument("--original-audit", default="/tmp/minic-p2-x86-direct-audit.json")
    parser.add_argument("--minic", default="build/release/bin/minic-cc")
    parser.add_argument("--gnu-as", default="x86_64-linux-gnu-gcc")
    parser.add_argument("--output", default="/tmp/minic-x86-kvm25-replay-v1")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    manifest_file = pathlib.Path(args.frozen) / "manifest.json"
    try:
        manifest = json.loads(manifest_file.read_text())
        previous = json.loads(pathlib.Path(args.original_audit).read_text())
    except (OSError, ValueError) as exc:
        print(f"BLOCKED original frozen evidence inaccessible: {exc}", file=sys.stderr)
        return 2
    old_kvm = {failure["object"] for failure in previous["failures"]
               if failure["object"].startswith("arch/x86/kvm/")}
    rows = [row for row in manifest["rows"] if row["object"] in old_kvm]
    if len(rows) != 25 or len(old_kvm) != 25 or \
            manifest.get("identity", {}).get("target") != "x86_64":
        print(f"BLOCKED owner identity/count mismatch: matched={len(rows)} failures={len(old_kvm)}",
              file=sys.stderr)
        return 2
    if args.jobs < 1 or args.jobs > 12 or args.timeout < 1:
        parser.error("jobs must be 1..12 and timeout positive")
    minic_path = pathlib.Path(args.minic).resolve()
    gnu_as_executable = shutil.which(args.gnu_as)
    if not minic_path.is_file() or not gnu_as_executable:
        print("BLOCKED compiler pair missing: " + str(minic_path) + " / " + args.gnu_as,
              file=sys.stderr)
        return 2
    minic_sha_before = digest(minic_path)
    gnu_as_path = pathlib.Path(gnu_as_executable).resolve()
    gnu_as_sha_before = digest(gnu_as_path)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda item: one(item, args), rows))
    statuses = {"MINIC_S_GNU_AS_PASS": 0, "MINIC_S_FAIL": 0,
                "GNU_AS_FAIL": 0, "BLOCKED": 0}
    for result in results:
        statuses[result["stage"]] += 1
        print(f'{result["stage"]:21} {result["object"]} '
              f'{result.get("minic_error") or result.get("gnu_as_error") or result.get("error", "")}',
              flush=True)
    minic_sha_after = digest(minic_path)
    gnu_as_sha_after = digest(gnu_as_path)
    pair_unchanged = minic_sha_before == minic_sha_after and \
        gnu_as_sha_before == gnu_as_sha_after
    report = {"schema": "minic-linux-x86-kvm25-vector-replay-v1",
              "compiler_pair": {"minic_path": str(minic_path),
                                "minic_sha256_before": minic_sha_before,
                                "minic_sha256_after": minic_sha_after,
                                "gnu_as_path": str(gnu_as_path),
                                "gnu_as_sha256_before": gnu_as_sha_before,
                                "gnu_as_sha256_after": gnu_as_sha_after,
                                "unchanged_during_replay": pair_unchanged},
              "manifest_sha256": digest(manifest_file),
              "original_audit_sha256": digest(pathlib.Path(args.original_audit)),
              "frozen_identity": manifest.get("identity", {}),
              "selection": "exact 25 arch/x86/kvm failures from frozen 2939 P2 audit",
              "statuses": statuses, "rows": results,
              "verdict": "ALL_25_MINIC_S_GNU_AS_PASS" if pair_unchanged and statuses["MINIC_S_GNU_AS_PASS"] == 25
              else "FAIL_CLOSED_NOT_FULLY_PASSED"}
    pathlib.Path(args.output).mkdir(parents=True, exist_ok=True)
    report_path = pathlib.Path(args.output) / "report.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"RESULT {report['verdict']} statuses={statuses} report={report_path}")
    return 0 if pair_unchanged and statuses["MINIC_S_GNU_AS_PASS"] == 25 else 1


if __name__ == "__main__":
    sys.exit(main())
