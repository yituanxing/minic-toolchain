#!/usr/bin/env python3
"""Strict semantic GNU vs MiniC Linux/QEMU P0/P1 differential.

The Linux serial stream contains nondeterministic timestamps, addresses and
boot ordering.  Compare the existing *user-space runtime contract* instead of
raw boot-log bytes, while independently rejecting kernel and QEMU failures.
This is a consumer of TWO actually executed images, never an image producer.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path


TARGETS = ("riscv64", "x86_64", "aarch64", "loongarch64")
LINUX_SOURCE_SHA256 = "dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932"
P0_MARKERS = (
    "MINIC_LINUX_RUNTIME_INIT_BEGIN",
    "INIT_PROC_MOUNT=PASS",
    "INIT_SYSFS_MOUNT=PASS",
    "INIT_DEVTMPFS_MOUNT=PASS",
    "INIT_CMDLINE_READ=PASS",
    "INIT_TMPFS_MOUNT=PASS",
    "INIT_TMPFS_RW=PASS",
    "USER_SHELL_OK",
    "DONE_COMMANDS",
    "MINIC_LINUX_RUNTIME_INIT_END fail=0",
)
P1_SYSCALLS = (
    "eventfd-rw", "epoll-eventfd", "timerfd-read", "signalfd-read",
    "inotify-create", "socketpair-rw", "ipv6-tcp-loopback", "memfd-rw",
    "pidfd-open", "futex-wake", "io-uring-setup", "mmap-mprotect",
)
# probe.c emits a distinct successful loopback-interface setup event inside
# test_ipv6_tcp(), between socketpair and the twelve counted syscalls' IPv6
# result. This is NOT a thirteenth syscall, but MUST appear exactly once.
P1_EXPECTED_EVENTS = (*P1_SYSCALLS[:6], "loopback-up", *P1_SYSCALLS[6:])
P1_MARKERS = (
    "RUNTIME_V2_SYSCALL_BEGIN",
    "RUNTIME_V2_SYSCALL_END pass=12 fail=0",
    "INIT_RUNTIME_PROBE=PASS",
)
BAD_KERNEL = re.compile(
    r"(?:Kernel panic|\bOops:|\bBUG:|general protection fault|"
    r"Unable to handle kernel|KASAN:|UBSAN:|RUNTIME_V2_SYSCALL_FAIL|"
    r"INIT_FAST_FAIL|INIT_POWEROFF_ERROR|INIT_[A-Z_]+(?:=FAIL|_ERROR)|"
    r"MINIC_LINUX_RUNTIME_INIT_END fail=[1-9])",
    re.IGNORECASE,
)
EVENT = re.compile(r"^RUNTIME_V2_SYSCALL_PASS ([a-z][a-z0-9-]*)$")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
GNU_VERDICT = "GNU_QEMU_P0_P1_PASS_NOT_MINIC"
MINIC_VERDICT = "MINIC_QEMU_P0_P1_PASS_NO_GCC_C_FALLBACK"
MINIC_PRE_DIFF_VERDICT = "MINIC_QEMU_P0_P1_BOOTED_DIFF_PENDING"


class RuntimeMismatch(ValueError):
    pass


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalized_lines(log: str) -> list[str]:
    # QEMU serial may emit carriage returns and harmless ANSI styling.
    log = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", log.replace("\r", ""))
    return [line.strip() for line in log.splitlines()]


def inspect(log: str, label: str, profile: str, release: str) -> dict:
    lines = normalized_lines(log)
    for line in lines:
        if BAD_KERNEL.search(line):
            raise RuntimeMismatch(f"{label}: kernel/user-space failure: {line[:200]}")

    if not any(f"Linux version {release}" in line for line in lines):
        raise RuntimeMismatch(f"{label}: missing expected Linux release {release}")
    cmdlines = [line for line in lines if "Kernel command line:" in line]
    if len(cmdlines) != 1 or "rdinit=/init" not in cmdlines[0]:
        raise RuntimeMismatch(f"{label}: missing or ambiguous rdinit=/init boot")
    if profile == "p1" and "minic_runtime=p1" not in cmdlines[0]:
        raise RuntimeMismatch(f"{label}: P1 kernel command line is missing")
    if profile == "p0" and "minic_runtime=p1" in cmdlines[0]:
        raise RuntimeMismatch(f"{label}: P0 unexpectedly ran the P1 probe")
    if not any("Run /init as init process" in line or
               "Starting init: /init" in line for line in lines):
        raise RuntimeMismatch(f"{label}: Linux did not hand off to PID1")

    required = (*P0_MARKERS, *(P1_MARKERS if profile == "p1" else ()))
    for marker in required:
        count = sum(line == marker for line in lines)
        if count != 1:
            raise RuntimeMismatch(f"{label}: marker {marker!r} occurs {count} times")
    init_order = [lines.index(marker) for marker in P0_MARKERS]
    if init_order != sorted(init_order):
        raise RuntimeMismatch(f"{label}: initramfs milestones out of execution order")
    if profile == "p0" and any(line.startswith("RUNTIME_V2_SYSCALL_") for line in lines):
        raise RuntimeMismatch(f"{label}: P0 unexpectedly contains syscall P1 events")

    passed = [match.group(1) for line in lines
              if (match := EVENT.fullmatch(line)) is not None]
    if profile == "p1":
        for name in P1_SYSCALLS:
            if passed.count(name) != 1:
                raise RuntimeMismatch(f"{label}: P1 syscall {name} count={passed.count(name)}")
        if passed != list(P1_EXPECTED_EVENTS):
            raise RuntimeMismatch(f"{label}: P1 syscall/auxiliary order, count, or identity changed: {passed}")
    elif passed:
        raise RuntimeMismatch(f"{label}: unexpected P1 events in P0")

    return {"profile": profile, "release": release,
            "init_markers": list(required),
            "syscall_events": [name for name in passed if name in P1_SYSCALLS],
            "auxiliary_events": [name for name in passed if name == "loopback-up"]}


def compare(golden: str, candidate: str, profile: str, release: str) -> dict:
    expected = inspect(golden, "GNU", profile, release)
    actual = inspect(candidate, "MiniC", profile, release)
    if expected != actual:
        raise RuntimeMismatch(f"GNU/MiniC semantic-event mismatch: {expected} != {actual}")
    return actual


def check_receipts(gnu: dict, minic: dict, target: str, profile: str,
                   gnu_log: Path, minic_log: Path,
                   allow_pending_diff: bool = False) -> dict:
    """Require independently authenticated runtime and all-MiniC ownership.

    A passing log alone is never proof: a skipped QEMU job, old Image, or a
    GCC C-object fallback must not be promotable to a runtime verdict.
    """
    if not isinstance(gnu, dict) or not isinstance(minic, dict):
        raise RuntimeMismatch("missing structured QEMU proof")
    # Only the differential verifier itself may consume a boot-complete but
    # not-yet-compared candidate. Every other caller still requires the final
    # accepted verdict, which cannot be written before BOTH comparisons pass.
    admitted = {MINIC_VERDICT}
    if allow_pending_diff:
        admitted.add(MINIC_PRE_DIFF_VERDICT)
    if gnu.get("verdict") != GNU_VERDICT or minic.get("verdict") not in admitted:
        raise RuntimeMismatch("GNU and MiniC complete P0/P1 verdicts required")
    for name, receipt in (("GNU", gnu), ("MiniC", minic)):
        if receipt.get("target") != target:
            raise RuntimeMismatch(f"{name}: wrong target")
        for key in ("source_sha256", "config_sha256", "initramfs_sha256", "image_sha256"):
            value = receipt.get(key)
            if not isinstance(value, str) or not SHA256.fullmatch(value) or value == "0" * 64:
                raise RuntimeMismatch(f"{name}: invalid {key}")
        firmware = receipt.get("firmware_sha256")
        if (target == "loongarch64" and
                (not isinstance(firmware, str) or not SHA256.fullmatch(firmware))):
            raise RuntimeMismatch(f"{name}: LoongArch requires SHA-pinned firmware")
        if firmware is not None and (not isinstance(firmware, str) or not SHA256.fullmatch(firmware)):
            raise RuntimeMismatch(f"{name}: invalid firmware SHA")
        lanes = receipt.get("lanes")
        if (not isinstance(lanes, list) or len(lanes) != 2 or
                {x.get("lane") for x in lanes if isinstance(x, dict)} != {"p0", "p1"}):
            raise RuntimeMismatch(f"{name}: must prove exactly P0 and P1")

    for key in ("source_sha256", "config_sha256", "initramfs_sha256", "firmware_sha256"):
        if gnu.get(key) != minic.get(key):
            raise RuntimeMismatch(f"GNU/MiniC used different {key}")
    if gnu.get("source_sha256") != LINUX_SOURCE_SHA256:
        raise RuntimeMismatch("not the pinned original Linux 6.6.143 source archive")

    owners = minic.get("golden_c_owners")
    produced = minic.get("minic_c_owners")
    gnu_owners = gnu.get("gnu_c_owners")
    if (type(owners) is not int or type(produced) is not int or owners < 200
            or produced != owners or type(gnu_owners) is not int
            or owners != gnu_owners or minic.get("gnu_c_fallback_count") != 0):
        raise RuntimeMismatch("all original ELF64 C owners must be MiniC, zero GCC C fallback")
    for key in ("compiler_sha256", "compiler_driver_sha256", "compiler_cc_sha256"):
        if (not isinstance(minic.get(key), str) or
                not SHA256.fullmatch(minic[key]) or minic[key] == "0" * 64):
            raise RuntimeMismatch(f"missing authenticated MiniC executable SHA: {key}")
    if minic["compiler_driver_sha256"] != minic["compiler_sha256"]:
        raise RuntimeMismatch("the authenticated MiniC driver identity changed")

    chosen = {}
    for name, receipt, log in (("GNU", gnu, gnu_log), ("MiniC", minic, minic_log)):
        lane = next(x for x in receipt["lanes"] if x["lane"] == profile)
        expected = "GNU_QEMU_RUNTIME_PASS" if name == "GNU" else "MINIC_QEMU_RUNTIME_PASS"
        if (lane.get("verdict") != expected or type(lane.get("exit_code")) is not int
                or lane["exit_code"] != 0 or lane.get("timed_out") is not False
                or not isinstance(lane.get("qemu_command"), list)
                or not lane["qemu_command"]):
            raise RuntimeMismatch(f"{name}: skipped, timed-out, or failed {profile} QEMU")
        if lane.get("log_sha256") != digest(log):
            raise RuntimeMismatch(f"{name}: altered or mismatched {profile} QEMU log")
        chosen[name] = lane
    return {"golden_image_sha256": gnu["image_sha256"],
            "minic_image_sha256": minic["image_sha256"],
            "gnu_c_fallback_count": 0, "minic_c_owners": produced}


def self_test() -> None:
    def fixture(profile: str) -> str:
        lines = [f"Linux version 6.6.143 (GNU)",
                 "Kernel command line: console=ttyS0 rdinit=/init" +
                 (" minic_runtime=p1" if profile == "p1" else ""),
                 "Run /init as init process", *P0_MARKERS]
        if profile == "p1":
            lines += [P1_MARKERS[0], *(f"RUNTIME_V2_SYSCALL_PASS {x}" for x in P1_EXPECTED_EVENTS),
                      P1_MARKERS[1], P1_MARKERS[2]]
        return "\r\n".join(lines) + "\n"

    negatives = 0
    for profile in ("p0", "p1"):
        source = fixture(profile)
        assert compare(source, source, profile, "6.6.143")["profile"] == profile
        mutations = [
            source.replace("INIT_PROC_MOUNT=PASS", "INIT_PROC_MOUNT=FAIL"),
            source.replace("rdinit=/init", "rdinit=/bin/sh"),
            source.replace("Linux version 6.6.143", "Linux version 6.6.144"),
            source + "Kernel panic - not syncing\n",
            source.replace("USER_SHELL_OK", ""),
            source.replace("DONE_COMMANDS", "DONE_COMMANDS\nDONE_COMMANDS"),
        ]
        if profile == "p1":
            mutations += [
                source.replace("RUNTIME_V2_SYSCALL_PASS eventfd-rw", ""),
                source.replace("RUNTIME_V2_SYSCALL_PASS eventfd-rw", "RUNTIME_V2_SYSCALL_FAIL eventfd-rw"),
                source.replace("minic_runtime=p1", ""),
                source.replace("RUNTIME_V2_SYSCALL_PASS loopback-up", ""),
                source.replace("RUNTIME_V2_SYSCALL_PASS loopback-up", "RUNTIME_V2_SYSCALL_PASS unknown-extra"),
                source.replace("RUNTIME_V2_SYSCALL_PASS loopback-up", "RUNTIME_V2_SYSCALL_PASS loopback-up\nRUNTIME_V2_SYSCALL_PASS loopback-up"),
                source.replace("RUNTIME_V2_SYSCALL_PASS loopback-up\n", "")
                      + "RUNTIME_V2_SYSCALL_PASS loopback-up\n",
                source.replace("RUNTIME_V2_SYSCALL_PASS ipv6-tcp-loopback", "RUNTIME_V2_SYSCALL_PASS loopback-up\nRUNTIME_V2_SYSCALL_PASS ipv6-tcp-loopback"),
            ]
        for sample in mutations:
            try:
                compare(source, sample, profile, "6.6.143")
            except RuntimeMismatch:
                negatives += 1
            else:
                raise AssertionError("incorrectly accepted invalid QEMU runtime")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        gnu_log = work / "gnu.log"
        minic_log = work / "minic.log"
        gnu_log.write_text(fixture("p1"))
        minic_log.write_text(fixture("p1"))
        common = dict(target="aarch64", source_sha256=LINUX_SOURCE_SHA256,
                      config_sha256="b" * 64, initramfs_sha256="c" * 64,
                      firmware_sha256=None, image_sha256="d" * 64)
        def lanes(which: str) -> list[dict]:
            return [dict(lane=profile, verdict=f"{which}_QEMU_RUNTIME_PASS",
                         exit_code=0, timed_out=False,
                         qemu_command=["qemu-system-aarch64", "-kernel", "Image"],
                         log_sha256=digest(gnu_log if which == "GNU" else minic_log))
                    for profile in ("p0", "p1")]
        gnu = dict(common, verdict=GNU_VERDICT, lanes=lanes("GNU"), gnu_c_owners=4078)
        minic = dict(common, verdict=MINIC_VERDICT, lanes=lanes("MINIC"),
                     golden_c_owners=4078, minic_c_owners=4078,
                     gnu_c_fallback_count=0, compiler_sha256="e" * 64,
                     compiler_driver_sha256="e" * 64,
                     compiler_cc_sha256="f" * 64)
        assert check_receipts(gnu, minic, "aarch64", "p1", gnu_log, minic_log)["minic_c_owners"] == 4078

        def bad(mutator) -> None:
            nonlocal negatives
            left, right = copy.deepcopy(gnu), copy.deepcopy(minic)
            mutator(left, right)
            try:
                check_receipts(left, right, "aarch64", "p1", gnu_log, minic_log)
            except RuntimeMismatch:
                negatives += 1
            else:
                raise AssertionError("incorrectly accepted invalid provenance")
        bad(lambda a, b: b.update(gnu_c_fallback_count=1))
        bad(lambda a, b: b.update(minic_c_owners=4077))
        bad(lambda a, b: a.update(gnu_c_owners=4077))
        bad(lambda a, b: a.update(source_sha256="a" * 64) or b.update(source_sha256="a" * 64))
        bad(lambda a, b: b.update(config_sha256="f" * 64))
        bad(lambda a, b: b.update(compiler_sha256="wrong"))
        bad(lambda a, b: b.update(compiler_cc_sha256="wrong"))
        bad(lambda a, b: b.update(compiler_driver_sha256="d" * 64))
        bad(lambda a, b: b.update(verdict=MINIC_PRE_DIFF_VERDICT))
        pending = copy.deepcopy(minic)
        pending["verdict"] = MINIC_PRE_DIFF_VERDICT
        assert check_receipts(gnu, pending, "aarch64", "p1", gnu_log, minic_log,
                              allow_pending_diff=True)["minic_c_owners"] == 4078
        bad(lambda a, b: b["lanes"][1].update(timed_out=True))
        bad(lambda a, b: b["lanes"][1].update(exit_code=124))
        bad(lambda a, b: b["lanes"][1].update(log_sha256="0" * 64))
        bad(lambda a, b: b["lanes"].pop())
        bad(lambda a, b: a.update(verdict="GNU_BUILD_ONLY"))
        bad(lambda a, b: b.update(verdict="MINIC_S_ONLY"))
        for sample in (fixture("p1") + "RUNTIME_V2_SYSCALL_PASS extra-syscall\n",
                       fixture("p1") + "INIT_FAST_FAIL\n",
                       fixture("p1").replace("RUNTIME_V2_SYSCALL_PASS eventfd-rw\r\nRUNTIME_V2_SYSCALL_PASS epoll-eventfd", "RUNTIME_V2_SYSCALL_PASS epoll-eventfd\r\nRUNTIME_V2_SYSCALL_PASS eventfd-rw")):
            try:
                compare(fixture("p1"), sample, "p1", "6.6.143")
            except RuntimeMismatch:
                negatives += 1
            else:
                raise AssertionError("accepted unexpected runtime event")
    print(f"MINIC_LINUX_RUNTIME_DIFF_SELFTEST=PASS negative_cases={negatives}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--golden-log", type=Path)
    parser.add_argument("--minic-log", type=Path)
    parser.add_argument("--golden-receipt", type=Path)
    parser.add_argument("--minic-receipt", type=Path)
    parser.add_argument("--target", choices=TARGETS)
    parser.add_argument("--profile", choices=("p0", "p1"))
    parser.add_argument("--linux-release", default="6.6.143")
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            return 0
        if not all((args.golden_log, args.minic_log, args.golden_receipt,
                    args.minic_receipt, args.target, args.profile, args.receipt)):
            raise RuntimeMismatch("both logs, QEMU receipts, target, profile and output required")
        provenance = check_receipts(json.loads(args.golden_receipt.read_text()),
                                    json.loads(args.minic_receipt.read_text()),
                                    args.target, args.profile, args.golden_log, args.minic_log,
                                    allow_pending_diff=True)
        result = compare(args.golden_log.read_text(errors="replace"),
                         args.minic_log.read_text(errors="replace"),
                         args.profile, args.linux_release)
        receipt = {"schema": "minic-linux-qemu-runtime-diff-v1",
                   "verdict": "GNU_MINIC_RUNTIME_SEMANTIC_MATCH",
                   "target": args.target, **result, **provenance,
                   "gnu_log_sha256": digest(args.golden_log),
                   "minic_log_sha256": digest(args.minic_log)}
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(receipt, sort_keys=True, indent=2) + "\n")
        print(f"MINIC_LINUX_RUNTIME_DIFF=PASS target={args.target} profile={args.profile} "
              "GNU_AND_MINIC_ACTUALLY_BOOTED=1")
        return 0
    except (OSError, ValueError, AssertionError) as exc:
        print(f"MINIC_LINUX_RUNTIME_DIFF=FAIL reason={exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
