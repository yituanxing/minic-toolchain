#!/usr/bin/env python3
"""Read-only QMP register snapshots from an unchanged oversized RISC-V Image.

The complete initramfs/P1 oracle is deliberately NOT bypassed: this is a
kernel-only early-frontier diagnostic used solely after ROM/initrd overlap.
No gdb, custom QEMU binary, extra runner or unpinned compiler is required.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import signal
import socket
import subprocess
import time
from pathlib import Path

REGISTER = re.compile(r"(?im)(?:^|[ \t])(?:x[0-9]+/)?(pc|mepc|sepc|satp|mcause|scause|sp|ra|a0|a1|t0|t1)\s+((?:0x)?[0-9a-f]{1,16})(?=\s|$)")


def selected_registers(text: str) -> dict[str, str]:
    return {name.lower(): value.lower() for name, value in REGISTER.findall(text)}


def self_test() -> None:
    sample = "pc       0000000080200000\nsepc 0x0000000080201234\nsatp 0"
    got = selected_registers(sample)
    assert got == {"pc": "0000000080200000",
                   "sepc": "0x0000000080201234", "satp": "0"}
    kernel_registers = "x1/ra  ffffffff81234567 x2/sp  ffffffff8799ff00 x10/a0 0000000000000020"
    assert selected_registers(kernel_registers) == {
        "ra": "ffffffff81234567", "sp": "ffffffff8799ff00", "a0": "0000000000000020"
    }
    assert "zero" not in selected_registers(kernel_registers)
    print("LINUX_RUNTIME_QMP_SAMPLER_SELFTEST=PASS")


def qmp_call(reader, writer, cmd: dict) -> object:
    writer.write((json.dumps(cmd, separators=(",", ":")) + "\r\n").encode())
    writer.flush()
    while True:
        raw = reader.readline()
        if not raw:
            raise RuntimeError("QMP closed without a command result")
        msg = json.loads(raw)
        if "return" in msg:
            return msg["return"]
        if "error" in msg:
            raise RuntimeError(f"QMP command rejected: {msg['error']}")


def sample(image: Path, out: Path, timeout_s: int, qemu: str, samples: list[int],
           cmdline: str, stack_words: int) -> int:
    out.mkdir(parents=True, exist_ok=True)
    # GitHub Actions workspaces regularly exceed Linux's 108-byte AF_UNIX
    # sun_path limit. Use a short, unique per-process path instead of
    # nesting the QMP socket under build/linux-runtime-.../trials/full_all.
    socket_path = Path(f"/tmp/minic-qmp-{os.getpid()}.sock")
    if socket_path.exists() or socket_path.is_symlink():
        socket_path.unlink()
    console_path = out / "qemu-early-noinitrd.log"
    result_path = out / "qemu-registers.jsonl"
    started = time.monotonic()
    argv = [qemu, "-M", "virt", "-cpu", "max", "-m", "512M",
            "-smp", "1", "-nographic", "-no-reboot", "-bios", "default",
            "-kernel", str(image), "-append",
            cmdline,
            "-qmp", f"unix:{socket_path},server=on,wait=off"]
    # Three QMP observations suffice to discriminate persistent earlycon
    # execution from post-earlycon progress without 18 seconds per trial.
    # Bound all sample points through the CLI; default is still the fast 1/4/8 probe.
    rc = 124
    with console_path.open("w") as console, result_path.open("w") as evidence:
        proc = subprocess.Popen(argv, stdout=console, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, start_new_session=True)
        try:
            conn = socket.socket(socket.AF_UNIX)
            conn.settimeout(4)
            for _ in range(100):
                if socket_path.exists():
                    try:
                        conn.connect(str(socket_path))
                        break
                    except (ConnectionRefusedError, FileNotFoundError):
                        pass
                if proc.poll() is not None:
                    raise RuntimeError(f"QEMU exited before QMP: rc={proc.returncode}")
                time.sleep(.05)
            else:
                raise RuntimeError("QMP UNIX socket not available")
            with conn, conn.makefile("rb") as reader, conn.makefile("wb") as writer:
                greeting = json.loads(reader.readline())
                if "QMP" not in greeting:
                    raise RuntimeError("invalid QMP greeting")
                qmp_call(reader, writer, {"execute": "qmp_capabilities"})
                for second in samples:
                    remaining = second - (time.monotonic() - started)
                    if remaining > 0:
                        time.sleep(remaining)
                    if proc.poll() is not None:
                        print(f"COHORT_QMP_EARLY=QEMU_EXITED rc={proc.returncode} at_s={second}",
                              flush=True)
                        break
                    raw = str(qmp_call(reader, writer, {
                        "execute": "human-monitor-command",
                        "arguments": {"command-line": "info registers", "cpu-index": 0}
                    }))
                    regs = selected_registers(raw)
                    # Read-only view of the kernel stack. In the reproducible
                    # earlycon.o assembly, match is stored at sp+0x10. Do not
                    # assume the value is a valid pointer or modify guest RAM.
                    stack_window = ""
                    if "sp" in regs:
                        try:
                            sp_addr = int(regs["sp"], 16)
                            stack_window = str(qmp_call(reader, writer, {
                                "execute": "human-monitor-command",
                                "arguments": {
                                    "command-line": f"x /{stack_words}gx 0x{sp_addr+(16 if stack_words==4 else 0):x}",
                                    "cpu-index": 0
                                }
                            }))[:max(650,stack_words*105)]
                        except (ValueError, RuntimeError, OSError) as exc:
                            stack_window = f"UNAVAILABLE: {exc}"
                        print(f"COHORT_QMP_STACK at_s={second} sp={regs['sp']} "
                              f"sp_plus_0x10={stack_window!r}", flush=True)
                    evidence.write(json.dumps({
                        "at_seconds": second, "registers": regs,
                        "stack_window": stack_window,
                        "raw": raw
                    }, sort_keys=True) + "\n")
                    evidence.flush()
                    print(f"COHORT_QMP_EARLY_SAMPLE at_s={second} "
                          f"pc={regs.get('pc', 'UNKNOWN')} "
                          f"sepc={regs.get('sepc', 'UNKNOWN')} "
                          f"mepc={regs.get('mepc', 'UNKNOWN')} "
                          f"satp={regs.get('satp', 'UNKNOWN')}", flush=True)
        except (OSError, ValueError, RuntimeError) as exc:
            print(f"COHORT_QMP_EARLY=UNAVAILABLE error={exc}", flush=True)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=4)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=4)
            rc = proc.returncode or rc
    if socket_path.exists():
        socket_path.unlink()
    print(f"COHORT_QMP_EARLY_COMPLETE wall_ms={int((time.monotonic()-started)*1000)} "
          f"qemu_rc={rc} samples={len(result_path.read_text().splitlines())}", flush=True)
    # The diagnostic must never claim a kernel-only boot passed the initramfs
    # runtime oracle. The caller interprets guest progress independently.
    return 0


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--image", type=Path)
    p.add_argument("--output-dir", type=Path)
    p.add_argument("--qemu", default="qemu-system-riscv64")
    p.add_argument("--timeout-seconds", type=int, default=25)
    p.add_argument("--kernel-cmdline", default="console=ttyS0 earlycon=sbi loglevel=8 panic=-1")
    p.add_argument("--stack-words", type=int, default=4)
    p.add_argument("--sample-seconds", default="1,4,8",
                   help="strictly increasing sampling times in seconds (max 110)")
    args = p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.image is None or args.output_dir is None:
        p.error("--image and --output-dir are required")
    if not args.image.is_file() or args.image.stat().st_size <= 0:
        p.error("image must be an existing non-empty file")
    if not 20 <= args.timeout_seconds <= 120:
        p.error("timeout must allow the register sample")
    try:
        moments = [int(v) for v in args.sample_seconds.split(",")]
    except ValueError:
        p.error("sample-seconds must be comma-separated integers")
    if (not moments or moments[0] < 1 or moments != sorted(set(moments))
            or moments[-1] > 110 or moments[-1] > args.timeout_seconds - 4):
        p.error("sample-seconds must be increasing, unique and fit timeout")
    if not 4 <= args.stack_words <= 96:
        p.error("stack-words must be 4..96")
    if len(args.kernel_cmdline)>256 or not args.kernel_cmdline.isascii():
        p.error("kernel-cmdline must be ASCII and at most 256 characters")
    return sample(args.image, args.output_dir, args.timeout_seconds, args.qemu,
                  moments, args.kernel_cmdline, args.stack_words)


if __name__ == "__main__":
    raise SystemExit(main())
