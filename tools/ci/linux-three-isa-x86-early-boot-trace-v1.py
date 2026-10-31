#!/usr/bin/env python3
"""Bounded x86 BIOS-only P0 failure diagnostic; never a certification retry.

Reuses the original GNU P0 QEMU CLI, unchanged candidate bzImage/initramfs,
and AArch64 diagnostic resource limits. Adds event logging and bounded QMP
register samples only; never recompiles, mutates objects, alters P0/P1 receipts
or infers a symbol/owner from a BIOS-only serial log.
"""
from __future__ import annotations
import argparse
from collections import Counter
import importlib.util
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time

spec = importlib.util.spec_from_file_location("minic_aarch_trace_limits",
    Path(__file__).with_name("linux-three-isa-aarch64-early-boot-trace-v1.py"))
common = importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)
VERDICT = common.VERDICT
DiagnosticError = common.DiagnosticError
QMP_REPLY_MAX = 65536
QMP_SOCKET_TIMEOUT = 0.3


def bios_only(raw):
    if len(raw) > 4096:
        return False
    text = re.sub(rb"\x1b(?:\[[0-?]*[ -/]*[@-~]|.)", b"", raw).decode("ascii", "replace")
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return (len(lines) == 2 and lines[0].startswith("SeaBIOS (version ") and
            lines[0].endswith(")") and lines[1] == "Booting from ROM...")


def verified_command(gnu, qemu, image, initramfs):
    if (gnu.get("target") != "x86_64" or gnu.get("verdict") != "GNU_QEMU_P0_P1_PASS_NOT_MINIC"
            or not common.HEX.fullmatch(str(gnu.get("image_sha256", "")))
            or gnu.get("initramfs_sha256") != common.sha(initramfs)
            or gnu["image_sha256"] == common.sha(image)):
        raise DiagnosticError("not same-initramfs original GNU x86 P0/P1 oracle")
    oracle = common.module()
    lanes = gnu.get("lanes", [])
    if not isinstance(lanes, list) or len(lanes) != 2:
        raise DiagnosticError("missing GNU dual-lane commands")
    for lane, name in zip(lanes, ("p0", "p1")):
        expected = oracle.boot_args("x86_64", qemu, image, initramfs, None, name, "2G")
        if lane.get("lane") != name or lane.get("qemu_command") != expected:
            raise DiagnosticError("GNU x86 QEMU invocation differs from current CLI")
    return oracle.boot_args("x86_64", qemu, image, initramfs, None, "p0", "2G")


def parse_trace(path, max_bytes):
    raw = path.read_text(errors="replace")
    vectors = Counter(re.findall(r"\bv=([0-9a-fA-F]{2})\b", raw))
    resets = len(re.findall(r"(?m)^CPU Reset \(CPU \d+\)", raw))
    triples = len(re.findall(r"Triple fault", raw, re.I))
    pcs = re.findall(r"\b(?:RIP|EIP|pc)=([0-9a-fA-F]+)|\bIP=([0-9a-fA-F]+:[0-9a-fA-F]+)", raw)
    registers = [line for line in raw.splitlines() if re.search(
        r"\b(?:RIP|EIP|EAX|RAX|CR0|CR2|CR3|CR4|EFER|CS|SS)=", line)]
    exceptions = [line for line in raw.splitlines() if re.search(
        r"check_exception|\bv=[0-9a-fA-F]{2}\b|Triple fault|CPU Reset|Invalid|error", line, re.I)]
    return dict(exception_vector_counts=dict(sorted(vectors.items())),
                triple_fault_observed=bool(triples), triple_fault_count=triples,
                cpu_reset_dump_count=resets,
                repeated_reset_observed=resets > 2,
                reset_loop_diagnosis="UNKNOWN: reset dumps alone do not prove a loop",
                observed_pc_samples=[a or b for a, b in pcs[-64:]],
                x86_register_dump_tail=registers[-96:], exception_reset_tail=exceptions[-64:],
                trace_bytes=path.stat().st_size, trace_sha256=common.sha(path),
                trace_at_byte_limit=path.stat().st_size >= max_bytes,
                no_event_does_not_prove_absence=True)


def qmp_reply(sock, expected_id):
    """Read one bounded QMP reply, skipping asynchronous event notifications."""
    received = bytearray()
    while len(received) < QMP_REPLY_MAX:
        chunk = sock.recv(min(8192, QMP_REPLY_MAX - len(received)))
        if not chunk:
            raise OSError("QMP disconnected before reply")
        received.extend(chunk)
        while b"\n" in received:
            raw, _, tail = received.partition(b"\n")
            received = bytearray(tail)
            reply = json.loads(raw)
            if expected_id is None and "QMP" in reply:
                return reply
            if expected_id is not None and reply.get("id") == expected_id:
                return reply
    raise OSError("QMP reply exceeded diagnostic byte bound")


def qmp_pc_sample(path, elapsed):
    """Observe the live CPU via QMP/HMP; no stop, step, reset or guest write."""
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(QMP_SOCKET_TIMEOUT)
        sock.connect(str(path))
        qmp_reply(sock, None)
        sock.sendall(b'{"execute":"qmp_capabilities","id":"enable"}\n')
        enabled = qmp_reply(sock, "enable")
        if "error" in enabled:
            raise OSError("QMP capabilities rejected")
        sock.sendall(b'{"execute":"human-monitor-command",'
                     b'"arguments":{"command-line":"info registers"},'
                     b'"id":"registers"}\n')
        reply = qmp_reply(sock, "registers")
        registers = reply.get("return")
        if not isinstance(registers, str):
            raise OSError("QMP info registers unavailable")
        pc = re.search(r"(?m)^(RIP|EIP)=([0-9a-fA-F]{4,16})\b", registers)
        if pc is None:
            raise OSError("QMP info registers lacks x86 instruction pointer")
        sample = {"elapsed_seconds": round(elapsed, 3),
                  "pc_register": pc.group(1), "pc": pc.group(2).lower()}
        for name in ("CS", "CR0", "EFER"):
            field = re.search(r"(?m)^" + name + r"\s*=\s*([0-9a-fA-F]+)", registers)
            if field:
                sample[name.lower()] = field.group(1).lower()
        return sample


def qmp_sample_schedule(timeout):
    """Sample at several seconds without extending the caller's time limit."""
    return [second for second in (0.2, 0.9, 1.6, 3.0, 5.0, 8.0, 12.0, 18.0, 25.0)
            if second < timeout - 0.2]


def run(image, initramfs, failed_p0, gnu_receipt, output, qemu, timeout=15, max_bytes=1048576):
    image = common.required(image, "candidate bzImage")
    initramfs = common.required(initramfs, "original initramfs")
    failed_p0 = common.required(failed_p0, "failed candidate P0")
    gnu_receipt = common.required(gnu_receipt, "original GNU receipt")
    qemu = common.executable(qemu, "x86 QEMU")
    if not bios_only(failed_p0.read_bytes()):
        raise DiagnosticError("only BIOS-only failed candidate P0 is eligible")
    if not 2 <= timeout <= 30 or not 65536 <= max_bytes <= 4 * 1024**2:
        raise DiagnosticError("timeout=2..30 seconds; max-bytes=64KiB..4MiB required")
    inputs = dict(image=image, initramfs=initramfs, failed_p0=failed_p0, gnu_receipt=gnu_receipt)
    before = {name: common.sha(path) for name, path in inputs.items()}
    command = verified_command(json.loads(gnu_receipt.read_text()), qemu, image, initramfs)
    output.mkdir(parents=True, exist_ok=False)
    trace, serial = output / "qemu-int-cpu-reset-guest-errors.log", output / "diagnostic-serial.log"
    started, timed_out, samples, qmp_failures = time.monotonic(), False, [], []
    # Keep the Unix socket short: GitHub Actions workspace paths can exceed
    # sockaddr_un's 108-byte bound. The directory is removed after QEMU exits.
    with tempfile.TemporaryDirectory(prefix="minic-x86-qmp-") as qmp_dir:
        qmp_socket = Path(qmp_dir) / "monitor.sock"
        debug = [*command, "-d", "int,cpu_reset,guest_errors", "-D", str(trace),
                 "-qmp", f"unix:{qmp_socket},server=on,wait=off"]
        with serial.open("wb") as stream:
            child = subprocess.Popen(debug, stdin=subprocess.DEVNULL, stdout=stream,
                                     stderr=subprocess.STDOUT, start_new_session=True,
                                     preexec_fn=common.limits(max_bytes, timeout))
            for due in qmp_sample_schedule(timeout):
                if child.poll() is not None:
                    break
                remaining = due - (time.monotonic() - started)
                if remaining > 0:
                    try:
                        child.wait(timeout=remaining)
                        break
                    except subprocess.TimeoutExpired:
                        pass
                if child.poll() is not None:
                    break
                try:
                    samples.append(qmp_pc_sample(qmp_socket, time.monotonic() - started))
                except (OSError, ValueError, json.JSONDecodeError) as exc:
                    # Failure to observe must never change the original runtime
                    # failure or pretend that the CPU stayed inside firmware.
                    qmp_failures.append(type(exc).__name__ + ": " + str(exc)[:120])
            try:
                status = child.wait(timeout=max(0, timeout - (time.monotonic() - started)))
            except subprocess.TimeoutExpired:
                timed_out = True
                os.killpg(child.pid, signal.SIGKILL)
                status = child.wait()
    if not trace.exists():
        trace.touch()
    report = dict(schema="minic-x86-early-boot-trace-v1", verdict=VERDICT,
                  target="x86_64", original_failed_p0_bios_only=True,
                  original_qemu_command=command, diagnostic_qemu_command=debug,
                  input_sha256=before, diagnostic_exit_code=status,
                  diagnostic_timed_out=timed_out, max_seconds=timeout, max_bytes=max_bytes,
                  elapsed_seconds=round(time.monotonic() - started, 3),
                  serial_bytes=serial.stat().st_size, serial_sha256=common.sha(serial),
                  qmp_pc_samples=samples, qmp_sample_attempts=len(samples) + len(qmp_failures),
                  qmp_observation=("LIVE_PC_SAMPLED" if samples else "UNAVAILABLE"),
                  qmp_errors=qmp_failures[:12],
                  **parse_trace(trace, max_bytes))
    report["original_inputs_unchanged"] = before == {n: common.sha(p) for n, p in inputs.items()}
    (output / "diagnostic-receipt.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    if not report["original_inputs_unchanged"]:
        raise DiagnosticError("original candidate/GNU evidence changed")
    print("X86_LINUX_EARLY_BOOT_TRACE=DIAGNOSTIC_ONLY "
          f"triple_fault={int(report['triple_fault_observed'])} reset_dumps={report['cpu_reset_dump_count']} "
          f"vectors={json.dumps(report['exception_vector_counts'], sort_keys=True)} "
          f"timeout={int(timed_out)} trace_bytes={report['trace_bytes']} "
          f"qmp_pc_samples={len(samples)} qmp_observation={report['qmp_observation']} "
          "candidate_verdict_unchanged=1 no_recompile=1")
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("image", "initramfs", "failed-p0", "gnu-receipt", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--qemu", type=Path, default=Path("/usr/bin/qemu-system-x86_64"))
    p.add_argument("--timeout", type=int, default=15)
    p.add_argument("--max-bytes", type=int, default=1048576)
    try:
        run(**vars(p.parse_args()))
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"X86_LINUX_EARLY_BOOT_TRACE=FAIL {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
