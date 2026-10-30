#!/usr/bin/env python3
"""Bounded AArch64 MiniC Linux early-boot QEMU failure diagnostic ONLY.

Runs only after an empty candidate P0 serial log, using the original GNU P0
QEMU command with the in-place MiniC Image and additional QEMU debug logging.
It never writes to the original Image, initramfs, failed log, GNU receipt, or
the candidate's pass/fail verdict. This is not a retry for certification.
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import resource
import signal
import subprocess
import sys
import time


SCHEMA = "minic-aarch64-early-boot-trace-v1"
VERDICT = "DIAGNOSTIC_ONLY_NEVER_GNU_OR_MINIC_RUNTIME_PASS"
HEX = re.compile(r"^([0-9a-f]{64})$")
PC_PATTERNS = (
    re.compile(r"\b(?:PC|ELR|pc|elr)\s*[:=]\s*(0x[0-9a-fA-F]+)"),
    re.compile(r"\b(?:PC|ELR|pc|elr)\s+(0x[0-9a-fA-F]+)"),
)
EXCEPTION_PATTERNS = (
    re.compile(r"(?im)^.{0,180}(?:Exception|Data Abort|Instruction Abort|prefetch abort|fault|Reset).{0,180}$"),
)


class DiagnosticError(ValueError):
    pass


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            digest.update(chunk)
    return digest.hexdigest()


def required(path: Path, label: str, allow_empty: bool = False) -> Path:
    if path.is_symlink() or not path.is_file() or (not allow_empty and path.stat().st_size == 0):
        raise DiagnosticError(f"missing/unsafe {label}: {path}")
    return path.resolve(strict=True)


def module():
    path = Path(__file__).with_name("linux-three-isa-gnu-qemu-v1.py")
    spec = importlib.util.spec_from_file_location("minic_qemu_reference", path)
    if spec is None or spec.loader is None:
        raise DiagnosticError("original three-ISA GNU QEMU command unavailable")
    imported = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(imported)
    return imported


def image_header(image: Path) -> dict:
    with image.open("rb") as stream:
        head = stream.read(64)
    if len(head) < 64:
        return {"bytes": len(head), "arm64_magic_valid": False,
                "reason": "short ARM64 boot Image"}
    # Linux Documentation/arch/arm64/booting.rst: magic at byte 0x38;
    # text_offset, image_size and flags at 0x08, 0x10 and 0x18.
    return {"bytes": 64, "arm64_magic_valid": head[56:60] == b"ARM\x64",
            "first_64_bytes_hex": head.hex(),
            "text_offset": int.from_bytes(head[8:16], "little"),
            "image_size": int.from_bytes(head[16:24], "little"),
            "flags": int.from_bytes(head[24:32], "little")}


def verified_qemu_command(gnu: dict, qemu: Path, image: Path,
                          initramfs: Path) -> list[str]:
    if (gnu.get("target") != "aarch64" or
            gnu.get("verdict") != "GNU_QEMU_P0_P1_PASS_NOT_MINIC" or
            not HEX.fullmatch(str(gnu.get("image_sha256", ""))) or
            gnu.get("initramfs_sha256") != sha(initramfs)):
        raise DiagnosticError("not original same-initramfs AArch64 GNU QEMU P0/P1 receipt")
    if gnu["image_sha256"] == sha(image):
        raise DiagnosticError("candidate Image matches GNU golden; not a MiniC diagnostic")
    lanes = gnu.get("lanes")
    if not isinstance(lanes, list) or len(lanes) != 2 or lanes[0].get("lane") != "p0":
        raise DiagnosticError("missing original GNU P0 invocation")
    original = lanes[0].get("qemu_command")
    if not isinstance(original, list) or not all(isinstance(x, str) for x in original):
        raise DiagnosticError("invalid original GNU QEMU command")
    oracle = module()
    expected = oracle.boot_args("aarch64", qemu, image, initramfs, None, "p0", "2G")
    if original != expected:
        raise DiagnosticError("GNU original P0 command differs from current candidate invocation")
    return expected


def parse_trace(path: Path) -> dict:
    raw = path.read_text(encoding="utf-8", errors="replace")
    pcs: list[str] = []
    for pattern in PC_PATTERNS:
        for match in pattern.finditer(raw):
            if match.group(1) not in pcs:
                pcs.append(match.group(1))
            if len(pcs) >= 12:
                break
        if len(pcs) >= 12:
            break
    exceptions: list[str] = []
    for pattern in EXCEPTION_PATTERNS:
        exceptions.extend(match.group().strip() for match in pattern.finditer(raw))
    return {"guest_pc_or_elr_samples": pcs[:12],
            "exception_or_reset_samples": exceptions[:20],
            "debug_trace_bytes": path.stat().st_size,
            "debug_trace_sha256": sha(path)}


def resolve_kernel_symbols(vmlinux: Path, nm: Path, pcs: list[str]) -> dict:
    """Read-only absolute VMA matching; do not invent an early KASLR slide."""
    vmlinux = required(vmlinux, "original GNU-linked MiniC kernel vmlinux")
    # Debian binutils may install a symlink to the actual .bfd executable.
    # Preserve the user-selected executable spelling in the receipt/argv.
    nm = executable(nm, "GNU AArch64 nm binary")
    with vmlinux.open("rb") as stream:
        hdr = stream.read(64)
    if (len(hdr) < 64 or hdr[:6] != b"\x7fELF\x02\x01" or
            int.from_bytes(hdr[18:20], "little") != 183):
        raise DiagnosticError("--vmlinux is not an AArch64 ELF64 kernel")
    probe = subprocess.run([str(nm), "-n", str(vmlinux)], text=True,
                           capture_output=True, timeout=15)
    if probe.returncode or len(probe.stdout) > 16 * 1024**2:
        raise DiagnosticError("bounded original GNU AArch64 nm failed")
    records: list[tuple[int, str, str]] = []
    for line in probe.stdout.splitlines():
        match = re.match(r"^([0-9a-fA-F]{16})\s+([A-Za-z])\s+(\S+)$", line)
        if match and match.group(2) not in ("a", "A", "U", "u"):
            records.append((int(match.group(1), 16), match.group(2), match.group(3)))
    records.sort()
    addresses = [record[0] for record in records]
    found = []
    for value in pcs:
        pc = int(value, 16)
        index = bisect.bisect_right(addresses, pc) - 1
        match = None
        if index >= 0 and pc - addresses[index] <= 0x10000:
            addr, typ, symbol = records[index]
            match = {"symbol": symbol, "symbol_type": typ,
                     "symbol_vma": f"0x{addr:x}", "offset": f"0x{pc - addr:x}"}
        found.append({"observed_pc": value, "absolute_vma_match": match,
                      "runtime_relocation_slide_unknown": match is None})
    return {"vmlinux_sha256": sha(vmlinux), "gnu_nm_binary": str(nm),
            "gnu_nm_sha256": sha(nm), "kernel_symbols_seen": len(records),
            "absolute_vma_matches": found}


def limits(max_bytes: int, timeout: int):
    # Child-only limits. QEMU must map the genuine 2G guest; retain ample
    # virtual address space while limiting CPU, file size and core dumps.
    def child():
        resource.setrlimit(resource.RLIMIT_FSIZE, (max_bytes, max_bytes))
        resource.setrlimit(resource.RLIMIT_AS, (12 * 1024**3, 12 * 1024**3))
        resource.setrlimit(resource.RLIMIT_CPU, (timeout + 5, timeout + 10))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    return child


def executable(path: Path, label: str) -> Path:
    path = Path(path)
    if (not path.is_absolute() or not path.is_file() or
            not path.resolve(strict=True).is_file() or
            not os.access(path, os.X_OK)):
        raise DiagnosticError(f"missing/unsafe executable {label}: {path}")
    return path


def run(image: Path, initramfs: Path, failed_p0: Path, gnu_receipt: Path,
        output: Path, qemu: Path, timeout: int, max_bytes: int,
        vmlinux: Path | None = None, nm: Path | None = None) -> dict:
    image = required(image, "current MiniC candidate Image")
    initramfs = required(initramfs, "original initramfs")
    failed_p0 = required(failed_p0, "failed candidate P0 serial log", allow_empty=True)
    gnu_receipt = required(gnu_receipt, "original GNU QEMU receipt")
    qemu = executable(qemu, "original QEMU binary")
    if failed_p0.stat().st_size != 0:
        raise DiagnosticError("P0 log was not empty; early-boot trace only applies to zero-byte failures")
    if not 2 <= timeout <= 30 or not 65536 <= max_bytes <= 4 * 1024**2:
        raise DiagnosticError("diagnostic limits must be timeout=2..30 seconds and bytes=64KiB..4MiB")
    original = json.loads(gnu_receipt.read_text())
    command = verified_qemu_command(original, qemu, image, initramfs)
    input_shas = {"image": sha(image), "initramfs": sha(initramfs),
                  "empty_p0": sha(failed_p0), "gnu_receipt": sha(gnu_receipt)}
    # Fresh separate diagnostic folder: never replace even a failed runtime
    # log, receipt or previous trace. Original QEMU argv stays intact except
    # for QEMU's independent debug output destination and trace categories.
    output.mkdir(parents=True, exist_ok=False)
    trace = output / "qemu-int-guest-errors.log"
    serial = output / "qemu-diagnostic-serial.log"
    debug_command = [*command, "-d", "int,guest_errors", "-D", str(trace)]
    started = time.monotonic()
    timed_out = False
    with serial.open("wb") as destination:
        child = subprocess.Popen(debug_command, stdin=subprocess.DEVNULL,
                                 stdout=destination, stderr=subprocess.STDOUT,
                                 start_new_session=True,
                                 preexec_fn=limits(max_bytes, timeout))
        try:
            status = child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(child.pid, signal.SIGKILL)
            status = child.wait()
    if not trace.exists():
        trace.write_bytes(b"")
    report = {"schema": SCHEMA, "verdict": VERDICT, "target": "aarch64",
              "original_failed_candidate_p0_was_empty": True,
              "gnu_image_sha256": original["image_sha256"],
              "candidate_image_sha256": input_shas["image"],
              "initramfs_sha256": input_shas["initramfs"],
              "original_qemu_command": command, "diagnostic_qemu_command": debug_command,
              "diagnostic_elapsed_seconds": round(time.monotonic() - started, 3),
              "diagnostic_exit_code": status, "diagnostic_timed_out": timed_out,
              "max_seconds": timeout, "max_trace_or_serial_bytes": max_bytes,
              "diagnostic_serial_bytes": serial.stat().st_size,
              "diagnostic_serial_sha256": sha(serial),
              "image_header": image_header(image), **parse_trace(trace)}
    if vmlinux is not None:
        try:
            if nm is None:
                raise DiagnosticError("--vmlinux requires GNU --nm")
            report["kernel_absolute_symbol_diagnostic"] = resolve_kernel_symbols(
                vmlinux, nm, report["guest_pc_or_elr_samples"])
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            # Losing optional ELF symbolization may never discard the raw
            # CPU exception trace, which remains the primary diagnostic.
            report["kernel_absolute_symbol_diagnostic"] = {
                "status": "UNAVAILABLE", "reason": str(exc)[:500]}
    unchanged = {"image": sha(image), "initramfs": sha(initramfs),
                 "empty_p0": sha(failed_p0), "gnu_receipt": sha(gnu_receipt)}
    report["original_inputs_unchanged"] = unchanged == input_shas
    (output / "diagnostic-receipt.json").write_text(json.dumps(report, indent=2,
                                                        sort_keys=True) + "\n")
    if not report["original_inputs_unchanged"]:
        raise DiagnosticError("original candidate/GNU evidence changed during diagnostic")
    print("AARCH64_LINUX_EARLY_BOOT_TRACE=DIAGNOSTIC_ONLY "
          f"exit={status} timeout={int(timed_out)} "
          f"serial_bytes={report['diagnostic_serial_bytes']} "
          f"trace_bytes={report['debug_trace_bytes']} "
          f"pc_samples={len(report['guest_pc_or_elr_samples'])} "
          f"exception_samples={len(report['exception_or_reset_samples'])} "
          "GNU_Image_unchanged=1 candidate_verdict_unchanged=1")
    return report


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--image", type=Path, required=True)
    p.add_argument("--initramfs", type=Path, required=True)
    p.add_argument("--failed-p0", type=Path, required=True)
    p.add_argument("--gnu-receipt", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--qemu", type=Path, default=Path("/usr/bin/qemu-system-aarch64"))
    p.add_argument("--timeout", type=int, default=15)
    p.add_argument("--max-bytes", type=int, default=1024 * 1024)
    p.add_argument("--vmlinux", type=Path,
                   help="optional unchanged candidate GNU-linked vmlinux ELF for read-only symbol mapping")
    p.add_argument("--nm", type=Path,
                   help="GNU AArch64 nm executable, required with --vmlinux")
    args = p.parse_args()
    try:
        run(args.image, args.initramfs, args.failed_p0, args.gnu_receipt,
            args.output, args.qemu, args.timeout, args.max_bytes,
            args.vmlinux, args.nm)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"AARCH64_LINUX_EARLY_BOOT_TRACE=FAIL {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
