#!/usr/bin/env python3
"""Bounded, read-only LoongArch EFI-to-Linux QMP diagnostic after a failed P0.

This is never a boot oracle: it replays the exact GNU oracle QEMU arguments
with only a QMP socket added, and its verdict cannot change P0/P1 acceptance.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time


SAMPLE_SECONDS = (1, 3, 8, 15)
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


def run_qmp(argv: list[str], socket_path: Path, output: Path) -> list[dict]:
    samples: list[dict] = []
    started = time.monotonic()
    with (output / "diagnostic-p0.log").open("wb") as console:
        process = subprocess.Popen(argv, stdin=subprocess.DEVNULL,
                                   stdout=console, stderr=subprocess.STDOUT)
        try:
            with connect_qmp(socket_path, process) as connection:
                with connection.makefile("rb") as reader, connection.makefile("wb") as writer:
                    greeting = json.loads(reader.readline())
                    if "QMP" not in greeting:
                        raise RuntimeError("invalid QMP greeting")
                    qmp_command(reader, writer, "qmp_capabilities")
                    for second in SAMPLE_SECONDS:
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
    parser.add_argument("--census", type=Path)
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
