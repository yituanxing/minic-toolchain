#!/usr/bin/env python3
"""Bounded, read-only LoongArch EFI-to-Linux QMP diagnostic after a failed P0.

This is never a boot oracle: it replays the exact GNU oracle QEMU arguments
with only a QMP socket added, and its verdict cannot change P0/P1 acceptance.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import zlib


SAMPLE_SECONDS = (1, 3, 8, 15)
GNU_REFERENCE_SECONDS = (1, 3, 5)
PC = re.compile(r"(?im)(?:^|\s)PC\s*[:=]?\s*(?:0x)?([0-9a-f]{8,16})(?=\s|$)")


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(chunk)
    return sha.hexdigest()


def qemu_oracle():
    path = Path(__file__).with_name("linux-three-isa-gnu-qemu-v1.py")
    spec = importlib.util.spec_from_file_location("minic_loong_original_qemu", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("original GNU QEMU oracle unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def diagnostic_argv(oracle, qemu: Path, image: Path, initramfs: Path,
                    firmware: Path, socket_path: Path) -> list[str]:
    original = oracle.boot_args("loongarch64", qemu, image, initramfs,
                                firmware, "p0", "2G")
    return [*original, "-qmp", f"unix:{socket_path},server=on,wait=off"]


def qmp_command(reader, writer, command: str, arguments: dict | None = None):
    request = {"execute": command}
    if arguments is not None:
        request["arguments"] = arguments
    writer.write((json.dumps(request) + "\r\n").encode("utf-8"))
    writer.flush()
    while True:
        line = reader.readline()
        if not line:
            raise RuntimeError("QMP disconnected")
        reply = json.loads(line)
        if "error" in reply:
            raise RuntimeError(f"QMP {command}: {reply['error']}")
        if "return" in reply:
            return reply["return"]


def connect_qmp(path: Path, process: subprocess.Popen):
    connection = socket.socket(socket.AF_UNIX)
    connection.settimeout(5)
    for _ in range(100):
        if process.poll() is not None:
            connection.close()
            raise RuntimeError(f"QEMU exited before QMP: {process.returncode}")
        if path.exists():
            try:
                connection.connect(str(path))
                return connection
            except (ConnectionRefusedError, FileNotFoundError):
                pass
        time.sleep(.05)
    connection.close()
    raise RuntimeError("QMP socket not available within 5 seconds")


def run_qmp(argv: list[str], socket_path: Path, output: Path,
            sample_seconds: tuple[int, ...] = SAMPLE_SECONDS,
            log_name: str = "diagnostic-p0.log") -> list[dict]:
    samples: list[dict] = []
    started = time.monotonic()
    with (output / log_name).open("wb") as console:
        process = subprocess.Popen(argv, stdin=subprocess.DEVNULL,
                                   stdout=console, stderr=subprocess.STDOUT)
        try:
            with connect_qmp(socket_path, process) as connection:
                with connection.makefile("rb") as reader, connection.makefile("wb") as writer:
                    greeting = json.loads(reader.readline())
                    if "QMP" not in greeting:
                        raise RuntimeError("invalid QMP greeting")
                    qmp_command(reader, writer, "qmp_capabilities")
                    for second in sample_seconds:
                        delay = second - (time.monotonic() - started)
                        if delay > 0:
                            time.sleep(delay)
                        if process.poll() is not None:
                            samples.append({"at_seconds": second,
                                            "qemu_exited": process.returncode})
                            break
                        paused = False
                        try:
                            qmp_command(reader, writer, "stop")
                            paused = True
                            status = qmp_command(reader, writer, "query-status")
                            if not isinstance(status, dict) or status.get("status") != "paused":
                                raise RuntimeError(f"QMP not paused: {status!r}")
                            raw = qmp_command(reader, writer, "human-monitor-command",
                                              {"command-line": "info registers", "cpu-index": 0})
                            if not isinstance(raw, str):
                                raise RuntimeError("HMP info registers not text")
                            match = PC.search(raw)
                            samples.append({"at_seconds": second, "pause_verified": True,
                                            "pc": f"0x{match.group(1)}" if match else None,
                                            "registers_hmp": raw[:65536]})
                        finally:
                            if paused and process.poll() is None:
                                qmp_command(reader, writer, "cont")
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
    return samples


def pe_layout(header: bytes) -> dict:
    """Read only PE layout fields; an EFI ImageBase is not a QEMU runtime load base."""
    if len(header) < 64 or header[:2] != b"MZ":
        raise ValueError("missing EFI MZ header")
    offset = struct.unpack_from("<I", header, 0x3c)[0]
    if offset + 24 > len(header) or header[offset:offset + 4] != b"PE\0\0":
        raise ValueError("missing EFI PE signature")
    machine, sections, _, _, _, optional_size, _ = struct.unpack_from(
        "<HHIIIHH", header, offset + 4)
    optional = offset + 24
    if (machine != 0x6264 or optional + optional_size > len(header)
            or optional_size < 112 or struct.unpack_from("<H", header, optional)[0] != 0x20b):
        raise ValueError("not a complete LoongArch64 PE32+ optional header")
    layout = {"machine": "0x6264", "pe_header_offset": offset,
              "image_base_preferred": struct.unpack_from("<Q", header, optional + 24)[0],
              "entry_rva": struct.unpack_from("<I", header, optional + 16)[0],
              "size_of_image": struct.unpack_from("<I", header, optional + 56)[0],
              "section_alignment": struct.unpack_from("<I", header, optional + 32)[0],
              "sections": []}
    start = optional + optional_size
    if sections > 32 or start + sections * 40 > len(header):
        raise ValueError("truncated EFI section table")
    for index in range(sections):
        item = start + index * 40
        name = header[item:item + 8].rstrip(b"\0").decode("ascii", errors="replace")
        virtual_size, rva, raw_size = struct.unpack_from("<III", header, item + 8)
        layout["sections"].append({"name": name, "rva": rva,
                                   "virtual_size": virtual_size, "raw_size": raw_size})
    return layout


def image_layout(path: Path) -> dict:
    """Bounded streaming zboot inspection: never archive the huge inner image."""
    with path.open("rb") as stream:
        outer = stream.read(65536)
        info = {"outer_efi": pe_layout(outer),
                "runtime_load_base_known": False,
                "runtime_symbol_mapping": "requires observed EFI/kernel load bias"}
        if outer[4:8] != b"zimg" or outer[24:28] != b"gzip":
            return info
        compressed_at, compressed_bytes = struct.unpack_from("<II", outer, 8)
        if (compressed_at < 4096 or compressed_bytes < 0
                or compressed_at + compressed_bytes > path.stat().st_size
                or compressed_bytes > 256 * 1024 * 1024):
            raise ValueError("invalid Loong EFI zboot compressed bounds")
        stream.seek(compressed_at)
        decoder = zlib.decompressobj(31)
        prefix = bytearray()
        total = 0
        remaining = compressed_bytes
        while remaining:
            chunk = stream.read(min(1024 * 1024, remaining))
            if not chunk:
                raise ValueError("truncated zboot gzip payload")
            remaining -= len(chunk)
            decoded = decoder.decompress(chunk)
            total += len(decoded)
            if len(prefix) < 65536:
                prefix.extend(decoded[:65536 - len(prefix)])
            if total > 512 * 1024 * 1024:
                raise ValueError("inner EFI exceeds diagnostic bound")
        if not decoder.eof:
            raise ValueError("inner EFI gzip did not terminate")
        info["inner_kernel"] = {"decompressed_bytes": total,
                                "pe": pe_layout(bytes(prefix))}
        info["zboot"] = {"compression": "gzip", "offset": compressed_at,
                         "compressed_bytes": compressed_bytes}
        return info


def symbol_evidence(system_map: Path, vmlinux: Path, nm: Path,
                    output: Path) -> dict:
    """Preserve authentic candidate link symbols, never infer a physical load bias."""
    if (not system_map.is_file() or system_map.is_symlink()
            or not 0 < system_map.stat().st_size <= 64 * 1024 * 1024):
        raise ValueError("candidate System.map absent or outside diagnostic bounds")
    if not vmlinux.is_file() or vmlinux.is_symlink():
        raise ValueError("candidate vmlinux absent")
    raw = system_map.read_bytes()
    (output / "candidate-System.map.gz").write_bytes(gzip.compress(raw, compresslevel=6))
    proc = subprocess.run([str(nm), "-n", "--defined-only", str(vmlinux)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=30, check=True)
    if len(proc.stdout) > 64 * 1024 * 1024:
        raise ValueError("candidate nm output outside diagnostic bounds")
    (output / "candidate-nm.txt.gz").write_bytes(gzip.compress(proc.stdout, compresslevel=6))
    return {"system_map_sha256": hashlib.sha256(raw).hexdigest(),
            "system_map_bytes": len(raw), "system_map_lines": raw.count(b"\n"),
            "system_map_archive": "candidate-System.map.gz",
            "vmlinux_sha256": digest(vmlinux), "vmlinux_bytes": vmlinux.stat().st_size,
            "nm_command": [str(nm), "-n", "--defined-only", str(vmlinux)],
            "nm_sha256": hashlib.sha256(proc.stdout).hexdigest(),
            "nm_lines": proc.stdout.count(b"\n"), "nm_archive": "candidate-nm.txt.gz",
            "pc_to_symbol": "not inferred: QMP PCs need observed runtime load bias"}


def self_test() -> None:
    oracle = qemu_oracle()
    qemu, image = Path("qemu-system-loongarch64"), Path("candidate.efi")
    ram, firmware, sock = Path("root.cpio.gz"), Path("QEMU_EFI.fd"), Path("/tmp/s.sock")
    argv = diagnostic_argv(oracle, qemu, image, ram, firmware, sock)
    assert argv[:-2] == oracle.boot_args("loongarch64", qemu, image, ram, firmware, "p0", "2G")
    assert argv[-2:] == ["-qmp", "unix:/tmp/s.sock,server=on,wait=off"]
    assert argv[argv.index("-kernel") + 1] == "candidate.efi"
    assert argv[argv.index("-bios") + 1] == "QEMU_EFI.fd"
    assert argv[argv.index("-initrd") + 1] == "root.cpio.gz"
    assert argv[argv.index("-append") + 1] == "console=ttyS0 loglevel=8 panic=-1 rdinit=/init"
    assert ([int(m.group(1), 16) for m in PC.finditer("PC 0x9000000010001234\n")]
            == [0x9000000010001234])
    assert PC.search("r0: 0x1234\n") is None
    events = io.BytesIO(b'{"event":"STOP"}\r\n{"return":"PC 0x9000000010001234"}\r\n')
    outgoing = io.BytesIO()
    assert qmp_command(events, outgoing, "human-monitor-command",
                       {"command-line": "info registers", "cpu-index": 0}) == "PC 0x9000000010001234"
    assert json.loads(outgoing.getvalue())["execute"] == "human-monitor-command"
    assert SAMPLE_SECONDS == (1, 3, 8, 15)
    assert GNU_REFERENCE_SECONDS == (1, 3, 5)
    header = bytearray(512)
    header[:2] = b"MZ"
    struct.pack_into("<I", header, 0x3c, 0x40)
    header[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", header, 0x44, 0x6264, 1, 0, 0, 0, 112, 0)
    struct.pack_into("<H", header, 0x58, 0x20b)
    struct.pack_into("<I", header, 0x58 + 16, 0x10000)
    struct.pack_into("<Q", header, 0x58 + 24, 0)
    struct.pack_into("<I", header, 0x58 + 56, 0x30000)
    header[0xc8:0xcd] = b".text"
    struct.pack_into("<III", header, 0xd0, 0x10000, 0x10000, 0x10000)
    layout = pe_layout(bytes(header))
    assert layout["entry_rva"] == 0x10000 and layout["size_of_image"] == 0x30000
    assert layout["sections"][0]["name"] == ".text"
    print("MINIC_LOONG_EFI_QMP_DIAGNOSTIC_SELFTEST=PASS")


def run(args: argparse.Namespace) -> None:
    original_log = args.failed_p0.read_text(encoding="utf-8", errors="replace")
    if ("EFI stub: Exiting boot services" not in original_log
            or "Linux version 6.6.143" in original_log):
        raise RuntimeError("not the observed failed EFI-to-Linux P0 frontier")
    census = json.loads(args.census.read_text(encoding="utf-8"))
    if census.get("verdict") != "POSTLINK_ALL_C_MINIC_SHA_PASS":
        raise RuntimeError("full postlink MiniC C SHA provenance absent")
    golden = json.loads(args.gnu_receipt.read_text(encoding="utf-8"))
    if golden.get("target") != "loongarch64" or golden.get("verdict") != "GNU_QEMU_P0_P1_PASS_NOT_MINIC":
        raise RuntimeError("authenticated GNU Loong P0/P1 golden unavailable")
    if (digest(args.firmware) != golden.get("firmware_sha256")
            or digest(args.initramfs) != golden.get("initramfs_sha256")):
        raise RuntimeError("diagnostic differs from pinned GNU firmware/initrd")
    image = args.image
    if not image.is_file() or image.is_symlink() or not 0 < image.stat().st_size <= 512 * 1024 * 1024:
        raise RuntimeError("candidate EFI image absent, symlink or larger than 512 MiB")
    candidate_sha = digest(image)
    if candidate_sha == golden["image_sha256"]:
        raise RuntimeError("candidate EFI image equals GNU golden; no MiniC diagnostic")
    if args.output.exists():
        raise RuntimeError("refusing to replace pre-existing diagnostic directory")
    args.output.mkdir(parents=True)
    preserved = args.output / "candidate-vmlinuz.efi"
    shutil.copyfile(image, preserved)
    if digest(preserved) != candidate_sha or digest(image) != candidate_sha:
        raise RuntimeError("EFI bytes changed during diagnostic copy")
    report = {"schema": "minic-loongarch64-efi-qmp-frontier-v1",
              "diagnostic_only": True, "strict_qemu_verdict_modified": False,
              "original_candidate_p0_failed": True,
              "image": {"candidate_sha256": candidate_sha,
                        "candidate_bytes": preserved.stat().st_size,
                        "gnu_golden_sha256": golden["image_sha256"],
                        "different_from_gnu_golden": True,
                        "preserved_path": str(preserved)},
              "firmware_sha256": golden["firmware_sha256"],
              "initramfs_sha256": golden["initramfs_sha256"],
              "sampling_seconds": list(SAMPLE_SECONDS), "samples": [],
              "verdict": "DIAGNOSTIC_NOT_RUN"}
    try:
        report["pe_layout"] = image_layout(preserved)
    except Exception as exc:
        report["pe_layout_error"] = f"{type(exc).__name__}: {exc}"
    if all((args.system_map, args.vmlinux, args.nm)):
        try:
            report["link_symbols"] = symbol_evidence(
                args.system_map, args.vmlinux, args.nm, args.output)
        except Exception as exc:
            report["link_symbols_error"] = f"{type(exc).__name__}: {exc}"
    try:
        with tempfile.TemporaryDirectory(prefix="minic-loong-qmp-") as temporary:
            socket_path = Path(temporary) / "monitor.sock"
            oracle = qemu_oracle()
            argv = diagnostic_argv(oracle, args.qemu, image, args.initramfs,
                                   args.firmware, socket_path)
            report["qemu_command"] = argv
            report["samples"] = run_qmp(argv, socket_path, args.output)
            full = (len(report["samples"]) == len(SAMPLE_SECONDS)
                    and all(sample.get("pause_verified") is True
                            for sample in report["samples"]))
            report["verdict"] = "DIAGNOSTIC_SAMPLES_CAPTURED" if full else "DIAGNOSTIC_PARTIAL"
    except Exception as exc:
        report["verdict"] = "DIAGNOSTIC_FAILED"
        report["diagnostic_error"] = f"{type(exc).__name__}: {exc}"
    if args.gnu_image is not None:
        reference = {"reference_only": True,
                     "same_firmware_initrd_qemu_arguments": True,
                     "sampling_seconds": list(GNU_REFERENCE_SECONDS), "samples": []}
        try:
            if (not args.gnu_image.is_file() or args.gnu_image.is_symlink()
                    or digest(args.gnu_image) != golden["image_sha256"]):
                raise RuntimeError("pinned pristine GNU image SHA does not match receipt")
            with tempfile.TemporaryDirectory(prefix="minic-loong-gnu-qmp-") as temporary:
                sock = Path(temporary) / "monitor.sock"
                argv = diagnostic_argv(qemu_oracle(), args.qemu, args.gnu_image,
                                       args.initramfs, args.firmware, sock)
                reference["qemu_command"] = argv
                reference["samples"] = run_qmp(
                    argv, sock, args.output, GNU_REFERENCE_SECONDS,
                    "gnu-diagnostic-p0.log")
                reference["verdict"] = "REFERENCE_SAMPLES_CAPTURED" if (
                    len(reference["samples"]) == len(GNU_REFERENCE_SECONDS)
                    and all(sample.get("pause_verified") is True
                            for sample in reference["samples"])) else "REFERENCE_PARTIAL"
        except Exception as exc:
            reference["verdict"] = "REFERENCE_FAILED"
            reference["error"] = f"{type(exc).__name__}: {exc}"
        report["gnu_qmp_reference"] = reference
    (args.output / "loong-efi-qmp-receipt.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"MINIC_LOONG_EFI_QMP={report['verdict']} "
          f"candidate_sha256={candidate_sha} "
          f"candidate_bytes={report['image']['candidate_bytes']} "
          f"gnu_sha256={golden['image_sha256']} samples={len(report['samples'])}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--image", type=Path)
    parser.add_argument("--initramfs", type=Path)
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--failed-p0", type=Path)
    parser.add_argument("--gnu-receipt", type=Path)
    parser.add_argument("--gnu-image", type=Path)
    parser.add_argument("--census", type=Path)
    parser.add_argument("--system-map", type=Path)
    parser.add_argument("--vmlinux", type=Path)
    parser.add_argument("--nm", type=Path)
    parser.add_argument("--qemu", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if any(value is None for value in (args.image, args.initramfs, args.firmware,
                                       args.failed_p0, args.gnu_receipt, args.census,
                                       args.qemu, args.output)):
        parser.error("all diagnostic paths required")
    run(args)


if __name__ == "__main__":
    main()
