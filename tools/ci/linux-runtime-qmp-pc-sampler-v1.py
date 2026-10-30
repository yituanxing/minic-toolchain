#!/usr/bin/env python3
"""Read-only QMP register snapshots from an unchanged RISC-V Linux Image.

The complete initramfs/P1 oracle is deliberately NOT bypassed.  The original
no-initrd probe is for ROM overlap; an optional mapped-initrd probe uses the
same loader as a timed-out full-2064 P0 run without changing its verdict.
No gdb, custom QEMU binary, extra runner or unpinned compiler is required.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import signal
import socket
import subprocess
import time
from pathlib import Path

REGISTER = re.compile(r"(?im)(?:^|[ \t])(?:x[0-9]+/)?(pc|mepc|sepc|satp|mcause|scause|stvec|mtvec|sstatus|mstatus|sscratch|mscratch|sie|sip|mie|mip|medeleg|mideleg|sp|ra|a[0-7]|t[0-6]|s(?:1[01]|[0-9]))\s+((?:0x)?[0-9a-f]{1,16})(?=\s|$)")
STACK_FIRST_WORD = re.compile(r"(?m)^[0-9a-fA-F]{8,16}:\s*0x([0-9a-fA-F]{1,16})\b")
MONITOR_WORD = re.compile(r"0x([0-9a-fA-F]{1,16})\b")



def selected_registers(text: str) -> dict[str, str]:
    return {name.lower(): value.lower() for name, value in REGISTER.findall(text)}


def qemu_argv(qemu: str, image: Path, socket_path: Path, cmdline: str,
              memory: str = "512M", dtb: Path | None = None,
              initramfs: Path | None = None, initrd_load_addr: int | None = None) -> list[str]:
    argv = [qemu, "-M", "virt", "-cpu", "max", "-m", memory,
            "-smp", "1", "-nographic", "-no-reboot", "-bios", "default",
            "-kernel", str(image)]
    if dtb is not None and initramfs is not None and initrd_load_addr is not None:
        argv.extend(["-dtb", str(dtb), "-device",
                     f"loader,file={initramfs},addr=0x{initrd_load_addr:x},force-raw=on"])
    argv.extend(["-append", cmdline,
                 "-qmp", f"unix:{socket_path},server=on,wait=off"])
    return argv


def image_sha256(image: Path) -> str:
    digest = hashlib.sha256()
    with image.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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
    assert selected_registers("x12/a2 0000000000000001 x31/t6 0000000000000002 x27/s11 0000000000000003") == {
        "a2": "0000000000000001", "t6": "0000000000000002",
        "s11": "0000000000000003"}
    assert selected_registers("stvec ffffffff80000000 sstatus 0000000000000120 "
                              "scause 000000000000000d mideleg 0000000000003666") == {
        "stvec": "ffffffff80000000", "sstatus": "0000000000000120",
        "scause": "000000000000000d", "mideleg": "0000000000003666"}
    captured_stack = "ff2000000000b010: 0xff6000000d9f1128 0x0000000000000000"
    candidate = STACK_FIRST_WORD.search(captured_stack)
    assert candidate and int(candidate.group(1), 16) == 0xff6000000d9f1128
    assert MONITOR_WORD.findall("ff6000000d9f1128: 0xdead4ead00010001 0xffffffff 0xdeadbeef") == [
        "dead4ead00010001", "ffffffff", "deadbeef"]
    plain = qemu_argv("qemu-system-riscv64", Path("Image"), Path("/tmp/test.sock"), "console=ttyS0")
    assert plain[plain.index("-m") + 1] == "512M"
    assert "-dtb" not in plain and "-device" not in plain and "-initrd" not in plain
    mapped = qemu_argv("qemu-system-riscv64", Path("Image"), Path("/tmp/test.sock"),
                       "console=ttyS0 earlycon=sbi loglevel=8 panic=-1 rdinit=/init",
                       "2G", Path("full-crossblock-virt.dtb"), Path("runtime-initramfs.cpio.gz"),
                       0xa0000000)
    assert mapped[mapped.index("-m") + 1] == "2G"
    assert mapped[mapped.index("-dtb") + 1] == "full-crossblock-virt.dtb"
    assert mapped[mapped.index("-device") + 1] == (
        "loader,file=runtime-initramfs.cpio.gz,addr=0xa0000000,force-raw=on")
    assert mapped[mapped.index("-append") + 1].endswith("rdinit=/init")
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
           cmdline: str, stack_words: int, stop_during_snapshot: bool = False,
           probe_lock_at_pc: int | None = None, memory: str = "512M",
           dtb: Path | None = None, initramfs: Path | None = None,
           initrd_load_addr: int | None = None) -> int:
    out.mkdir(parents=True, exist_ok=True)
    # GitHub Actions workspaces regularly exceed Linux's 108-byte AF_UNIX
    # sun_path limit. Use a short, unique per-process path instead of
    # nesting the QMP socket under build/linux-runtime-.../trials/full_all.
    socket_path = Path(f"/tmp/minic-qmp-{os.getpid()}.sock")
    if socket_path.exists() or socket_path.is_symlink():
        socket_path.unlink()
    console_path = out / ("qemu-mapped-initrd.log" if initramfs is not None
                          else "qemu-early-noinitrd.log")
    result_path = out / "qemu-registers.jsonl"
    started = time.monotonic()
    argv = qemu_argv(qemu, image, socket_path, cmdline, memory, dtb,
                     initramfs, initrd_load_addr)
    print(f"COHORT_QMP_LOADER mode={'mapped_initrd' if initramfs else 'no_initrd'} "
          f"memory={memory} kernel={image} dtb={dtb or 'none'} "
          f"initramfs={initramfs or 'none'} "
          f"initrd_load_addr={hex(initrd_load_addr) if initrd_load_addr is not None else 'none'} "
          f"cmdline={cmdline!r}",
          flush=True)
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
                    pause_started = time.monotonic() if stop_during_snapshot else None
                    if stop_during_snapshot:
                        qmp_call(reader, writer, {"execute": "stop"})
                        state = qmp_call(reader, writer, {"execute": "query-status"})
                        if not isinstance(state, dict) or state.get("status") != "paused":
                            raise RuntimeError(f"QMP stop did not pause guest: {state!r}")
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
                    lock_probe = None
                    # Diagnostic for the *exact* MiniC do_raw_spin_unlock+0x30
                    # call site. A linked vmlinux address must be supplied by
                    # the caller; no lock offset is inferred from ET_REL code.
                    # Stack word 0 is the saved raw_spinlock_t* at this site.
                    if probe_lock_at_pc is not None and regs.get("pc") and (
                        int(regs["pc"], 16) == probe_lock_at_pc
                    ):
                        lock_probe = {"site_pc": hex(probe_lock_at_pc)}
                        stack_match = STACK_FIRST_WORD.search(stack_window)
                        if stack_match is None:
                            lock_probe["status"] = "STACK_WORD_UNAVAILABLE"
                        else:
                            lock_ptr = int(stack_match.group(1), 16)
                            lock_probe["lock_pointer"] = hex(lock_ptr)
                            if lock_ptr < 0xff00000000000000 or lock_ptr & 3:
                                lock_probe["status"] = "INVALID_KERNEL_LOCK_POINTER"
                            else:
                                try:
                                    data = str(qmp_call(reader, writer, {
                                        "execute": "human-monitor-command",
                                        "arguments": {
                                            "command-line": f"x /4gx 0x{lock_ptr:x}",
                                            "cpu-index": 0
                                        }
                                    }))
                                    lock_probe["raw_memory"] = data
                                    words = [int(x, 16) for x in MONITOR_WORD.findall(data)]
                                    if len(words) >= 3:
                                        lock_probe.update({
                                            "status": "MEMORY_READ",
                                            "raw_lock_u32": f"0x{words[0] & 0xffffffff:08x}",
                                            "magic_u32": f"0x{words[0] >> 32:08x}",
                                            "owner_cpu_s32": ((words[1] & 0xffffffff) ^ 0x80000000) - 0x80000000,
                                            "owner_pointer": f"0x{words[2]:016x}"
                                        })
                                    else:
                                        lock_probe["status"] = "MEMORY_UNPARSEABLE"
                                except (ValueError, RuntimeError, OSError) as exc:
                                    lock_probe["status"] = "MEMORY_UNAVAILABLE"
                                    lock_probe["error"] = str(exc)
                        print("COHORT_QMP_LOCK_PROBE at_s=" + str(second) +
                              " data=" + json.dumps(lock_probe, sort_keys=True), flush=True)
                    pause_duration_ms = 0
                    if stop_during_snapshot:
                        qmp_call(reader, writer, {"execute": "cont"})
                        pause_duration_ms = int((time.monotonic() - pause_started) * 1000)
                        print(f"COHORT_QMP_PAUSED_SNAPSHOT at_s={second} duration_ms={pause_duration_ms}", flush=True)
                    evidence.write(json.dumps({
                        "at_seconds": second, "registers": regs,
                        "stack_window": stack_window,
                        "stop_during_snapshot": stop_during_snapshot,
                        "pause_duration_ms": pause_duration_ms,
                        "lock_probe": lock_probe,
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
    p.add_argument("--memory", choices=("512M", "1G", "2G"), default="512M")
    p.add_argument("--dtb", type=Path,
                   help="same immutable device tree used by mapped-initrd runtime boot")
    p.add_argument("--initramfs", type=Path,
                   help="same original initramfs as the mapped-initrd runtime boot")
    p.add_argument("--initrd-load-addr", type=lambda value: int(value, 0),
                   help="physical loader address used by the original QEMU run")
    p.add_argument("--expected-image-sha256",
                   help="refuse to sample unless the Image matches the linked trial")
    p.add_argument("--timeout-seconds", type=int, default=25)
    p.add_argument("--kernel-cmdline", default="console=ttyS0 earlycon=sbi loglevel=8 panic=-1")
    p.add_argument("--stack-words", type=int, default=4)
    p.add_argument("--stop-during-snapshot", action="store_true",
                   help="QMP stop/read registers and stack/cont for consistent diagnostic snapshots")
    p.add_argument("--probe-lock-at-pc", type=lambda value: int(value, 0),
                   help="exact linked MiniC do_raw_spin_unlock+0x30 PC; read saved lock object at this site")
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
    mapping_requested = [args.dtb is not None, args.initramfs is not None,
                         args.initrd_load_addr is not None]
    if any(mapping_requested) and not all(mapping_requested):
        p.error("--dtb, --initramfs and --initrd-load-addr are required together")
    if all(mapping_requested):
        if not args.dtb.is_file() or not args.initramfs.is_file() or args.initramfs.stat().st_size == 0:
            p.error("mapped-initrd requires an existing DTB and non-empty initramfs")
        if args.initrd_load_addr <= 0 or args.initrd_load_addr >= 0x100000000:
            p.error("initrd-load-addr must be a nonzero address below 4GiB")
        guest_ram_end = 0x80000000 + {"512M": 512 << 20, "1G": 1 << 30,
                                     "2G": 2 << 30}[args.memory]
        image_end = 0x80200000 + args.image.stat().st_size
        initrd_end = args.initrd_load_addr + args.initramfs.stat().st_size
        if (image_end > args.initrd_load_addr or
                initrd_end > guest_ram_end or initrd_end > 0x100000000):
            p.error("mapped-initrd must not overlap kernel Image or exceed guest RAM")
    if args.expected_image_sha256 is not None:
        if not re.fullmatch(r"[0-9a-f]{64}", args.expected_image_sha256):
            p.error("expected-image-sha256 must be lowercase SHA256")
        actual_sha256 = image_sha256(args.image)
        if actual_sha256 != args.expected_image_sha256:
            p.error("immutable Image SHA256 mismatch")
        print(f"COHORT_QMP_IMAGE_IDENTITY=PASS image_sha={actual_sha256} "
              f"image_bytes={args.image.stat().st_size}")
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
    if args.probe_lock_at_pc is not None and not args.stop_during_snapshot:
        p.error("--probe-lock-at-pc requires --stop-during-snapshot")
    if len(args.kernel_cmdline)>256 or not args.kernel_cmdline.isascii():
        p.error("kernel-cmdline must be ASCII and at most 256 characters")
    return sample(args.image, args.output_dir, args.timeout_seconds, args.qemu,
                  moments, args.kernel_cmdline, args.stack_words,
                  args.stop_during_snapshot, args.probe_lock_at_pc,
                  args.memory, args.dtb, args.initramfs, args.initrd_load_addr)


if __name__ == "__main__":
    raise SystemExit(main())
