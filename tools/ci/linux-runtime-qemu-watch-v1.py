#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

TERMINAL = {"SAME_FAULT", "REGRESSED", "MOVED_LATER", "FRONTIER_PASS"}


def snapshot_interrupts(trace: Path, interrupts: Path):
    if not trace.exists():
        interrupts.write_text("")
        return
    lines = []
    with trace.open("r", errors="replace") as src:
        for line in src:
            if "riscv_cpu_do_interrupt" in line:
                lines.append(line)
    interrupts.write_text("".join(lines))


def classify(repo: Path, config: Path, nm: Path, trace: Path, console: Path,
             interrupts: Path, qemu_rc: int, json_out: Path, text_out: Path):
    snapshot_interrupts(trace, interrupts)
    cmd = [
        sys.executable,
        str(repo / "tools/ci/linux-runtime-frontier-v1.py"),
        "--config", str(config),
        "--interrupts", str(interrupts),
        "--nm", str(nm),
        "--trace", str(trace),
        "--console", str(console),
        "--qemu-rc", str(qemu_rc),
        "--json-out", str(json_out),
        "--text-out", str(text_out),
    ]
    proc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not json_out.exists():
        return None, proc.returncode
    try:
        result = json.loads(json_out.read_text())
    except (OSError, json.JSONDecodeError):
        return None, proc.returncode
    return result, proc.returncode


def stop_process(proc: subprocess.Popen):
    if proc.poll() is not None:
        return proc.returncode
    proc.terminate()
    try:
        return proc.wait(timeout=0.5)
    except subprocess.TimeoutExpired:
        proc.kill()
        return proc.wait(timeout=1.0)


def main():
    parser = argparse.ArgumentParser(description="Run QEMU until the frontier oracle becomes decisive.")
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--nm", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--timeout-ms", type=int, default=8000)
    parser.add_argument("--poll-ms", type=int, default=100)
    parser.add_argument("--repo", type=Path, default=Path.cwd())
    args = parser.parse_args()

    repo = args.repo.resolve()
    ev = args.evidence_dir.resolve()
    ev.mkdir(parents=True, exist_ok=True)
    trace = ev / "qemu.trace.log"
    console = ev / "qemu.console.log"
    interrupts = ev / "qemu.interrupts.txt"
    probe_json = ev / "watch-probe.json"
    probe_text = ev / "watch-probe.txt"
    final_json = ev / "frontier-result.json"
    final_text = ev / "frontier-summary.txt"

    for path in (trace, console, interrupts, probe_json, probe_text, final_json, final_text):
        try:
            path.unlink()
        except FileNotFoundError:
            pass
    trace.touch()
    console.touch()

    qemu_cmd = [
        "qemu-system-riscv64",
        "-M", "virt", "-cpu", "max", "-m", "512M", "-smp", "1",
        "-nographic", "-no-reboot", "-bios", "default",
        "-kernel", str(args.image.resolve()),
        "-append", "console=ttyS0 earlycon=sbi loglevel=8 panic=-1",
        "-d", "int,in_asm", "-D", str(trace),
    ]

    start = time.monotonic()
    deadline = start + args.timeout_ms / 1000.0
    polls = 0
    stop_reason = "hard-timeout"
    terminal_probe = None

    with console.open("wb") as console_fp:
        proc = subprocess.Popen(qemu_cmd, stdout=console_fp, stderr=subprocess.STDOUT)
        qemu_rc = None
        while True:
            now = time.monotonic()
            native_rc = proc.poll()
            if native_rc is not None:
                qemu_rc = native_rc
                stop_reason = "qemu-exit"
                break
            if now >= deadline:
                qemu_rc = stop_process(proc)
                stop_reason = "hard-timeout"
                break

            # The classifier ignores the known relocate_enable_mmu transition fault,
            # so a terminal verdict here is already sufficient evidence to stop.
            result, _ = classify(
                repo, args.config.resolve(), args.nm.resolve(), trace, console,
                interrupts, 124, probe_json, probe_text,
            )
            polls += 1
            if result and result.get("verdict") in TERMINAL:
                terminal_probe = result
                stop_reason = f'oracle:{result["verdict"]}'
                qemu_rc = stop_process(proc)
                break
            time.sleep(max(args.poll_ms, 10) / 1000.0)

    elapsed_ms = int((time.monotonic() - start) * 1000)
    if qemu_rc is None:
        qemu_rc = 124

    final_result, classifier_rc = classify(
        repo, args.config.resolve(), args.nm.resolve(), trace, console,
        interrupts, qemu_rc, final_json, final_text,
    )
    if final_result is None:
        print("QEMU_WATCH=FAIL reason=no-final-classification", file=sys.stderr)
        return 3

    verdict = final_result["verdict"]
    probe_verdict = terminal_probe.get("verdict") if terminal_probe else "none"
    print(f"QEMU_WATCH=PASS stop={stop_reason} elapsed_ms={elapsed_ms} polls={polls} qemu_rc={qemu_rc}")
    print(f"QEMU_WATCH_PROBE_VERDICT={probe_verdict}")
    print(final_text.read_text(), end="")

    (ev / "watch-result.json").write_text(json.dumps({
        "schema": 1,
        "stop_reason": stop_reason,
        "elapsed_ms": elapsed_ms,
        "polls": polls,
        "qemu_rc": qemu_rc,
        "probe_verdict": probe_verdict,
        "final_verdict": verdict,
        "classifier_rc": classifier_rc,
    }, indent=2, sort_keys=True) + "\n")

    return classifier_rc


if __name__ == "__main__":
    raise SystemExit(main())
