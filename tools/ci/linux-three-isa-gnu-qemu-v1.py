#!/usr/bin/env python3
"""Fail-closed GNU Linux 6.6.143 QEMU P0/P1 boot oracle for three new ISAs.

This certifies *only* a GNU-built Linux image.  It does not run MiniC, replace
any C object, or establish GNU/MiniC equivalence.  Both target-native initramfs
and kernel must boot before they can serve as a MiniC runtime differential
reference.  In particular, LoongArch needs an explicit EFI image and firmware.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time


LINUX_SHA256 = "dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932"
LINUX_RELEASE = "6.6.143"
TARGETS = {
    "x86_64": {
        "machine": 62, "gnu": "gcc", "qemu": "qemu-system-x86_64",
        "image": "arch/x86/boot/bzImage", "console": "ttyS0",
        "machine_args": ("-machine", "pc", "-cpu", "max"),
    },
    "aarch64": {
        "machine": 183, "gnu": "aarch64-linux-gnu-gcc",
        "qemu": "qemu-system-aarch64", "image": "arch/arm64/boot/Image",
        "console": "ttyAMA0", "machine_args": ("-machine", "virt", "-cpu", "cortex-a57"),
    },
    "loongarch64": {
        "machine": 258, "gnu": "loongarch64-linux-gnu-gcc-13",
        "qemu": "qemu-system-loongarch64",
        "image": "arch/loongarch/boot/vmlinux.efi", "console": "ttyS0",
        "machine_args": ("-machine", "virt", "-cpu", "la464"),
    },
}

P0_MARKERS = (
    "INIT_PROC_MOUNT=PASS",
    "INIT_SYSFS_MOUNT=PASS",
    "INIT_DEVTMPFS_MOUNT=PASS",
    "INIT_CMDLINE_READ=PASS",
    "INIT_TMPFS_MOUNT=PASS",
    "INIT_TMPFS_RW=PASS",
    "MINIC_LINUX_RUNTIME_INIT_END fail=0",
    "USER_SHELL_OK",
    "DONE_COMMANDS",
)
P1_SYSCALLS = (
    "eventfd-rw", "epoll-eventfd", "timerfd-read", "signalfd-read",
    "inotify-create", "socketpair-rw", "ipv6-tcp-loopback", "memfd-rw",
    "pidfd-open", "futex-wake", "io-uring-setup", "mmap-mprotect",
)
# probe.c's bring_loopback_up() emits a separately observable success before
# the IPv6 check.  It is an auxiliary setup operation, *not* one of the 12
# syscall test cases counted by RUNTIME_V2_SYSCALL_END pass=12.  The authentic
# x86_64 GNU Linux QEMU P1 oracle includes this thirteenth PASS line; require
# that exact extra event rather than weakening the total-event assertion.
P1_SUCCESS_EVENTS = (*P1_SYSCALLS[:6], "loopback-up", *P1_SYSCALLS[6:])
P1_SUCCESS_LINE = re.compile(r"^RUNTIME_V2_SYSCALL_PASS ([a-z][a-z0-9-]*)$", re.MULTILINE)
FATAL = re.compile(
    r"Kernel panic|\bOops:|\bBUG:|\bKASAN:|\bUBSAN:|out.of.bounds|"
    r"general protection fault|Unable to handle kernel|"
    r"Internal error: Oops|INIT_FAST_FAIL|INIT_POWEROFF_ERROR|"
    r"RUNTIME_V2_SYSCALL_FAIL|INIT_RUNTIME_PROBE=FAIL",
    re.IGNORECASE,
)


class CertificationError(Exception):
    def __init__(self, message: str, lane_receipt: dict | None = None) -> None:
        super().__init__(message)
        self.lane_receipt = lane_receipt


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def require_file(path: Path, label: str) -> Path:
    if not path.is_file() or path.stat().st_size == 0:
        raise CertificationError(f"missing or empty {label}: {path}")
    return path.resolve()


def executable(name: str) -> Path:
    result = shutil.which(name)
    if not result:
        raise CertificationError(f"missing executable: {name}")
    return require_file(Path(result), name)


def command_line(argv: list[str]) -> str:
    result = subprocess.run(argv, check=True, text=True, capture_output=True, timeout=15)
    return result.stdout.strip().splitlines()[0]


def read_elf_machine(path: Path) -> tuple[int, int]:
    with path.open("rb") as stream:
        header = stream.read(64)
    if len(header) < 64 or header[:4] != b"\x7fELF" or header[5] != 1:
        raise CertificationError(f"invalid target initramfs ELF: {path}")
    return header[4], struct.unpack_from("<H", header, 18)[0]


def validate_log(log: str, lane: str, console: str) -> None:
    if lane not in ("p0", "p1"):
        raise CertificationError(f"unknown lane: {lane}")
    # Serial consoles may use CRLF; normalize for semantic markers while the
    # receipt still hashes and retains the original unmodified log bytes.
    log = log.replace("\r\n", "\n").replace("\r", "")
    fatal = FATAL.search(log)
    if fatal:
        raise CertificationError(f"{lane} fatal guest marker: {fatal.group(0)}")
    mandatory = [
        f"Linux version {LINUX_RELEASE}",
        "MINIC_LINUX_RUNTIME_INIT_BEGIN",
        "Run /init as init process",
        *P0_MARKERS,
    ]
    # Linux can also log "Starting init: /init" rather than "Run /init...".
    if "Run /init as init process" not in log and "Starting init: /init" in log:
        mandatory.remove("Run /init as init process")
    missing = [marker for marker in mandatory if marker not in log]
    if not re.search(r"Kernel command line:.*\brdinit=/init\b", log):
        missing.append("cmdline rdinit=/init")
    if not re.search(rf"Kernel command line:.*\bconsole={re.escape(console)}\b", log):
        missing.append(f"cmdline console={console}")
    if lane == "p0":
        if "RUNTIME_V2_SYSCALL_BEGIN" in log or P1_SUCCESS_LINE.search(log):
            missing.append("unexpected P1 execution/events in P0")
    else:
        if not re.search(r"Kernel command line:.*\bminic_runtime=p1\b", log):
            missing.append("cmdline minic_runtime=p1")
        mandatory_p1 = ("RUNTIME_V2_SYSCALL_BEGIN", "RUNTIME_V2_SYSCALL_END pass=12 fail=0",
                        "INIT_RUNTIME_PROBE=PASS")
        missing.extend(marker for marker in mandatory_p1 if marker not in log)
        actual = P1_SUCCESS_LINE.findall(log)
        if actual != list(P1_SUCCESS_EVENTS):
            missing.append("exactly 12 P1 syscall events plus the ordered "
                           f"loopback-up setup success; actual={actual}")
    if missing:
        raise CertificationError(f"{lane} missing/incorrect markers: {', '.join(missing)}")


def boot_args(target: str, qemu: Path, image: Path, initramfs: Path,
              firmware: Path | None, lane: str, memory: str) -> list[str]:
    spec = TARGETS[target]
    command = [str(qemu), *spec["machine_args"], "-m", memory, "-smp", "1",
               "-nographic", "-no-reboot",
               # P1 checks guest *loopback* networking only.  QEMU's default
               # implicit user-NIC on ARM virt loads the efi-virtio.rom PXE
               # option ROM, absent from minimal Ubuntu qemu-system-arm
               # installations.  Suppress that unused device explicitly;
               # do not let unrelated host-package ROM availability select
               # whether an original GNU kernel can enter QEMU.
               "-nic", "none"]
    if target == "loongarch64":
        if firmware is None:
            raise CertificationError("LoongArch64 requires EFI firmware (no fallback to -bios default)")
        command.extend(("-bios", str(firmware)))
    command.extend(("-kernel", str(image), "-initrd", str(initramfs)))
    cmdline = f"console={spec['console']} loglevel=8 panic=-1 rdinit=/init"
    if lane == "p1":
        cmdline += " minic_runtime=p1"
    command.extend(("-append", cmdline))
    return command


def run_lane(target: str, qemu: Path, image: Path, initramfs: Path,
             firmware: Path | None, lane: str, memory: str,
             timeout: int, output: Path) -> dict:
    argv = boot_args(target, qemu, image, initramfs, firmware, lane, memory)
    logfile = output / f"{lane}.log"
    started = time.monotonic()
    timed_out = False
    with logfile.open("wb") as stream:
        process = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=stream,
                                   stderr=subprocess.STDOUT)
        try:
            status = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            status = process.wait()
    elapsed = time.monotonic() - started
    receipt = {
        "lane": lane, "qemu_command": argv, "log": str(logfile),
        "log_sha256": digest(logfile), "elapsed_seconds": round(elapsed, 3),
        "exit_code": status, "timed_out": timed_out,
        "verdict": "GNU_QEMU_FAIL",
    }
    # A timeout is not success even if an early marker happened to be printed.
    if timed_out:
        raise CertificationError(f"{lane} QEMU timeout after {timeout}s; log={logfile}", receipt)
    if status != 0:
        raise CertificationError(f"{lane} QEMU exit code={status}; log={logfile}", receipt)
    log = logfile.read_text(encoding="utf-8", errors="replace")
    try:
        validate_log(log, lane, str(TARGETS[target]["console"]))
    except CertificationError as exc:
        raise CertificationError(str(exc), receipt) from exc
    receipt["verdict"] = "GNU_QEMU_RUNTIME_PASS"
    return receipt


def check_golden(target: str, work: Path, initramfs: Path,
                 rootfs: Path, firmware: Path | None,
                 firmware_sha256: str | None) -> tuple[Path, dict]:
    spec = TARGETS[target]
    evidence = work / "evidence"
    out = work / "build"
    source = require_file(work / f"linux-{LINUX_RELEASE}.tar.xz", "pinned Linux source")
    if digest(source) != LINUX_SHA256:
        raise CertificationError("Linux 6.6.143 source digest mismatch")
    manifest_path = require_file(evidence / "discovery.json", "GNU discovery")
    manifest = json.loads(manifest_path.read_text())
    if (manifest.get("verdict") != "GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC"
            or manifest.get("target") != target or manifest.get("config_profile") != "broad"):
        raise CertificationError("GNU target/profile/verdict mismatched")
    config = require_file(out / ".config", "GNU Kconfig")
    if digest(config) != manifest.get("config_sha256"):
        raise CertificationError("GNU Kconfig digest mismatch")
    gnu_image = require_file(out / manifest["kernel"], "GNU discovery kernel")
    if digest(gnu_image) != manifest.get("kernel_sha256"):
        raise CertificationError("GNU discovery kernel digest mismatch")
    image = require_file(out / spec["image"], "bootable GNU kernel image")
    if target == "loongarch64" and image == gnu_image:
        raise CertificationError("LoongArch64 EFI boot image must be separate from vmlinux")
    if target == "loongarch64" and firmware is None:
        raise CertificationError("LoongArch64 EFI firmware missing: provide SHA-verified QEMU_EFI.fd")
    if target == "loongarch64":
        if not firmware_sha256 or not re.fullmatch(r"[a-fA-F0-9]{64}", firmware_sha256):
            raise CertificationError("LoongArch64 requires expected EFI firmware SHA-256")
        if digest(firmware) != firmware_sha256.lower():
            raise CertificationError("LoongArch64 EFI firmware SHA-256 mismatch")
    elif firmware is not None or firmware_sha256:
        raise CertificationError("EFI firmware input is only valid for LoongArch64")
    require_file(initramfs, "target GNU initramfs")
    for relative in ("init", "bin/sh", "bin/runtime-probe"):
        elf = require_file(rootfs / relative, "target initramfs ELF")
        elf_class, machine = read_elf_machine(elf)
        if elf_class != 2 or machine != spec["machine"]:
            raise CertificationError(f"foreign initramfs ELF {relative}: class={elf_class} machine={machine}")
    result = {
        "schema": "minic-three-isa-gnu-qemu-v1", "target": target,
        "linux_release": LINUX_RELEASE, "source_sha256": LINUX_SHA256,
        "kconfig_profile": "broad", "config_sha256": digest(config),
        "gnu_c_owners": manifest["gnu_c_owners"],
        "gnu_c_owner_manifest_sha256": manifest["owner_manifest_sha256"],
        "gnu_linked_kernel": str(gnu_image),
        "gnu_linked_kernel_sha256": digest(gnu_image),
        "boot_image": str(image), "image_sha256": digest(image),
        "initramfs": str(initramfs), "initramfs_sha256": digest(initramfs),
        "initramfs_elf_machine": spec["machine"],
        "firmware": str(firmware) if firmware else None,
        "firmware_sha256": digest(firmware) if firmware else None,
        "scope": "GNU_KERNEL_QEMU_ORACLE_ONLY_NOT_MINIC",
    }
    return image, result


def self_test() -> None:
    sample = (f"Linux version {LINUX_RELEASE}\nKernel command line: console=ttyS0 "
              "rdinit=/init\nRun /init as init process\n"
              "MINIC_LINUX_RUNTIME_INIT_BEGIN\n" +
              "\n".join(P0_MARKERS) + "\n")
    validate_log(sample, "p0", "ttyS0")
    p1 = sample.replace("rdinit=/init\n", "rdinit=/init minic_runtime=p1\n")
    p1 += "RUNTIME_V2_SYSCALL_BEGIN\n"
    p1 += "".join(f"RUNTIME_V2_SYSCALL_PASS {name}\n" for name in P1_SUCCESS_EVENTS)
    p1 += "RUNTIME_V2_SYSCALL_END pass=12 fail=0\nINIT_RUNTIME_PROBE=PASS\n"
    validate_log(p1, "p1", "ttyS0")
    validate_log(p1.replace("\n", "\r\n"), "p1", "ttyS0")
    negatives = ((sample + "Kernel panic - not syncing\n", "p0"),
                 (sample + "RUNTIME_V2_SYSCALL_PASS eventfd-rw\n", "p0"),
                 (p1.replace("pass=12", "pass=11"), "p1"),
                 (p1.replace("RUNTIME_V2_SYSCALL_PASS loopback-up\n", ""), "p1"),
                 (p1.replace("RUNTIME_V2_SYSCALL_PASS loopback-up\n",
                             "RUNTIME_V2_SYSCALL_PASS loopback-up\n"
                             "RUNTIME_V2_SYSCALL_PASS loopback-up\n"), "p1"),
                 (p1.replace("RUNTIME_V2_SYSCALL_PASS loopback-up\n",
                             "RUNTIME_V2_SYSCALL_PASS unauthorized-extra\n"
                             "RUNTIME_V2_SYSCALL_PASS loopback-up\n"), "p1"),
                 (p1.replace("RUNTIME_V2_SYSCALL_PASS ipv6-tcp-loopback\n",
                             "RUNTIME_V2_SYSCALL_PASS ipv6-tcp-loopback\n"
                             "RUNTIME_V2_SYSCALL_PASS unauthorized-extra\n"), "p1"),
                 (p1.replace("RUNTIME_V2_SYSCALL_PASS loopback-up\n", "") +
                  "RUNTIME_V2_SYSCALL_PASS loopback-up\n", "p1"))
    for bad, lane in negatives:
        try:
            validate_log(bad, lane, "ttyS0")
        except CertificationError:
            pass
        else:
            raise AssertionError("negative runtime oracle falsely passed")
    assert "-bios" in boot_args("loongarch64", Path("qemu"), Path("kernel"),
                               Path("initramfs"), Path("QEMU_EFI.fd"), "p1", "2G")
    for target in TARGETS:
        command = boot_args(target, Path("qemu"), Path("kernel"),
                            Path("initramfs"),
                            Path("QEMU_EFI.fd") if target == "loongarch64" else None,
                            "p1", "2G")
        assert command.count("-nic") == 1
        assert command[command.index("-nic") + 1] == "none"
        assert "-nodefaults" not in command  # preserve the serial console
    try:
        boot_args("loongarch64", Path("qemu"), Path("kernel"), Path("initramfs"),
                  None, "p0", "2G")
    except CertificationError:
        pass
    else:
        raise AssertionError("accepted missing LoongArch EFI firmware")
    print("THREE_ISA_GNU_QEMU_SELFTEST=PASS targets=3 lanes=P0_P1 "
          f"panic_fail_closed=1 p1_negative_cases={len(negatives)} "
          "loong_efi_fail_closed=1 implicit_host_nic_disabled=1")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--target", choices=tuple(TARGETS))
    parser.add_argument("--gnu-work", type=Path)
    parser.add_argument("--initramfs", type=Path)
    parser.add_argument("--initramfs-root", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--firmware-sha256")
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--memory", choices=("1G", "2G", "4G"), default="2G")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if any(value is None for value in (args.target, args.gnu_work, args.initramfs,
                                       args.initramfs_root, args.output)):
        parser.error("--target --gnu-work --initramfs --initramfs-root --output required")
    if args.timeout < 30 or args.timeout > 900:
        parser.error("--timeout must be between 30 and 900 seconds")
    args.output.mkdir(parents=True, exist_ok=True)
    receipt_file = args.output / "gnu-qemu-receipt.json"
    if receipt_file.exists() or any((args.output / f"{lane}.log").exists()
                                    for lane in ("p0", "p1")):
        parser.error("output already contains boot evidence: refuse overwrite")
    receipt: dict = {"schema": "minic-three-isa-gnu-qemu-v1",
                     "target": args.target, "verdict": "GNU_QEMU_NOT_CERTIFIED", "lanes": []}
    try:
        firmware = require_file(args.firmware, "LoongArch EFI firmware") if args.firmware else None
        image, identity = check_golden(args.target, args.gnu_work,
                                       args.initramfs, args.initramfs_root, firmware,
                                       args.firmware_sha256)
        receipt.update(identity)
        qemu = executable(str(TARGETS[args.target]["qemu"]))
        gnu = executable(str(TARGETS[args.target]["gnu"]))
        triplet = command_line([str(gnu), "-dumpmachine"])
        if not triplet.startswith({"x86_64": "x86_64", "aarch64": "aarch64",
                                   "loongarch64": "loongarch64"}[args.target]):
            raise CertificationError(f"wrong GNU toolchain triplet: {triplet}")
        receipt.update({
            "qemu_binary_sha256": digest(qemu), "qemu_version": command_line([str(qemu), "--version"]),
            "gnu_compiler_binary_sha256": digest(gnu),
            "gnu_compiler_triplet": triplet,
            "gnu_compiler_version": command_line([str(gnu), "-dumpfullversion"]),
        })
        for lane in ("p0", "p1"):
            receipt["lanes"].append(run_lane(args.target, qemu, image, args.initramfs,
                                             firmware, lane, args.memory,
                                             args.timeout, args.output))
        receipt["verdict"] = "GNU_QEMU_P0_P1_PASS_NOT_MINIC"
        print(f"THREE_ISA_GNU_QEMU=PASS target={args.target} lanes=2/2 "
              f"image_sha256={receipt['image_sha256']} GNU_only=1")
        return_code = 0
    except (OSError, ValueError, CertificationError, subprocess.SubprocessError) as exc:
        receipt["verdict"] = "GNU_QEMU_FAIL"
        receipt["error"] = str(exc)
        if isinstance(exc, CertificationError) and exc.lane_receipt is not None:
            receipt["lanes"].append(exc.lane_receipt)
        print(f"THREE_ISA_GNU_QEMU=FAIL target={args.target} reason={exc}", file=sys.stderr)
        return_code = 2
    receipt_file.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
