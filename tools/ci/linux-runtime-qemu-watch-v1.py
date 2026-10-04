#!/usr/bin/env python3
import argparse
import bisect
import json
import re
import subprocess
import sys
import time
from collections import deque
from pathlib import Path

TERMINAL = {"SAME_FAULT", "REGRESSED", "MOVED_LATER", "FRONTIER_PASS"}
TRACE_PC_RE = re.compile(r"0x([0-9a-fA-F]+):")
FIRST_TARGET_SYMBOLS = (
    "spin_bug",
    "minic_spin_diag_bad_magic",
    "minic_spin_diag_recursion",
    "minic_spin_diag_cpu_recursion",
)


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


def probe_decisive(result):
    if not result:
        return False
    verdict = result.get("verdict")
    fault = result.get("fault")
    if verdict in ("SAME_FAULT", "REGRESSED", "FRONTIER_PASS"):
        return True
    if verdict == "MOVED_LATER":
        return fault is not None
    return verdict == "INCONCLUSIVE" and fault is not None


def stop_process(proc: subprocess.Popen):
    if proc.poll() is not None:
        return proc.returncode
    proc.terminate()
    try:
        return proc.wait(timeout=0.5)
    except subprocess.TimeoutExpired:
        proc.kill()
        return proc.wait(timeout=1.0)


def load_nm(path: Path):
    symbols = []
    by_name = {}
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            address = int(parts[0], 16)
        except ValueError:
            continue
        name = parts[2]
        symbols.append((address, name))
        by_name.setdefault(name, address)
    symbols.sort()
    return symbols, [address for address, _ in symbols], by_name


def resolve_pc(symbols, addresses, pc):
    index = bisect.bisect_right(addresses, pc) - 1
    if index < 0:
        return "?", 0
    base, name = symbols[index]
    return name, pc - base


def relocation_state(by_name, phys_entry):
    anchor = by_name.get("_start") or by_name.get("_text") or by_name.get("_stext")
    relocation_delta = anchor - phys_entry if anchor is not None and anchor > phys_entry else None
    linked_floor = phys_entry + relocation_delta if relocation_delta is not None else None
    return relocation_delta, linked_floor


def resolved_trace_entry(symbols, addresses, relocation_delta, linked_floor, pc, phys_entry):
    lookup = pc
    relocated = False
    if relocation_delta is not None and pc >= phys_entry and pc < linked_floor:
        lookup = pc + relocation_delta
        relocated = True
    symbol, offset = resolve_pc(symbols, addresses, lookup)
    return {
        "pc": pc,
        "lookup": lookup,
        "relocated": relocated,
        "symbol": symbol,
        "offset": offset,
    }


def trace_tail_symbols(trace: Path, nm: Path, phys_entry=0x80200000, limit=32):
    symbols, addresses, by_name = load_nm(nm)
    relocation_delta, linked_floor = relocation_state(by_name, phys_entry)
    tail = deque(maxlen=limit)
    last_key = None
    if not trace.exists():
        return []
    with trace.open("r", errors="replace") as src:
        for line in src:
            match = TRACE_PC_RE.search(line)
            if not match:
                continue
            pc = int(match.group(1), 16)
            entry = resolved_trace_entry(
                symbols, addresses, relocation_delta, linked_floor, pc, phys_entry)
            key = (entry["symbol"], entry["offset"])
            if key == last_key:
                continue
            last_key = key
            tail.append(entry)
    return list(tail)


def trace_first_symbol_context(trace: Path, nm: Path, target_names=FIRST_TARGET_SYMBOLS,
                               phys_entry=0x80200000, context_limit=64):
    if not trace.exists():
        return None
    symbols, addresses, by_name = load_nm(nm)
    present_targets = {name for name in target_names if name in by_name}
    if not present_targets:
        return None
    relocation_delta, linked_floor = relocation_state(by_name, phys_entry)
    history = deque(maxlen=context_limit)
    last_symbol = None
    with trace.open("r", errors="replace") as src:
        for line in src:
            match = TRACE_PC_RE.search(line)
            if not match:
                continue
            pc = int(match.group(1), 16)
            entry = resolved_trace_entry(
                symbols, addresses, relocation_delta, linked_floor, pc, phys_entry)
            symbol = entry["symbol"]
            if symbol in present_targets:
                return {
                    "target": symbol,
                    "entry": entry,
                    "context": list(history),
                }
            if symbol != last_symbol:
                history.append(entry)
                last_symbol = symbol
    return None


def trace_stall_context(trace: Path, nm: Path, tail_symbols, phys_entry=0x80200000,
                        context_limit=24, min_suffix_entries=64):
    if not trace.exists() or not tail_symbols:
        return None
    loop_symbols = {entry.get("symbol") for entry in tail_symbols if entry.get("symbol")}
    loop_symbols.discard("?")
    if not loop_symbols or len(loop_symbols) > 4:
        return None

    symbols, addresses, by_name = load_nm(nm)
    relocation_delta, linked_floor = relocation_state(by_name, phys_entry)
    history = deque(maxlen=context_limit)
    candidate_context = []
    suffix_entries = 0
    last_symbol = None

    with trace.open("r", errors="replace") as src:
        for line in src:
            match = TRACE_PC_RE.search(line)
            if not match:
                continue
            pc = int(match.group(1), 16)
            entry = resolved_trace_entry(
                symbols, addresses, relocation_delta, linked_floor, pc, phys_entry)
            symbol = entry["symbol"]
            if symbol in loop_symbols:
                if suffix_entries == 0:
                    candidate_context = list(history)
                suffix_entries += 1
                continue

            suffix_entries = 0
            if symbol != last_symbol:
                history.append(entry)
                last_symbol = symbol

    if suffix_entries < min_suffix_entries:
        return None
    return {
        "loop_symbols": sorted(loop_symbols),
        "suffix_entries": suffix_entries,
        "context": candidate_context,
    }


def console_hints(console: Path, limit=80):
    if not console.exists():
        return []
    needles = (
        "BUG:", "Oops", "spinlock", "lockup", "Unable to handle",
        "Kernel panic", "Call Trace", "epc :", "ra :", "status:",
    )
    hints = []
    for line in console.read_text(errors="replace").splitlines():
        if any(needle in line for needle in needles):
            hints.append(line)
    return hints[-limit:]


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

            result, _ = classify(
                repo, args.config.resolve(), args.nm.resolve(), trace, console,
                interrupts, 124, probe_json, probe_text,
            )
            polls += 1
            if probe_decisive(result):
                terminal_probe = result
                verdict = result.get("verdict", "unknown")
                suffix = ":fault" if verdict == "INCONCLUSIVE" else ""
                stop_reason = f"oracle:{verdict}{suffix}"
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
    tail_symbols = trace_tail_symbols(trace, args.nm.resolve())
    first_target = trace_first_symbol_context(trace, args.nm.resolve())
    stall = trace_stall_context(trace, args.nm.resolve(), tail_symbols)
    hints = console_hints(console)
    last = tail_symbols[-1] if tail_symbols else None
    print(f"QEMU_WATCH=PASS stop={stop_reason} elapsed_ms={elapsed_ms} polls={polls} qemu_rc={qemu_rc}")
    print(f"QEMU_WATCH_PROBE_VERDICT={probe_verdict}")
    if last:
        print(f'QEMU_WATCH_LAST_PC=0x{last["pc"]:x}')
        print(f'QEMU_WATCH_LAST_LOOKUP=0x{last["lookup"]:x}')
        print(f'QEMU_WATCH_LAST_SYMBOL={last["symbol"]}+0x{last["offset"]:x}')
    if first_target:
        context_names = [entry["symbol"] for entry in first_target["context"]]
        entry = first_target["entry"]
        print(f'QEMU_WATCH_FIRST_TARGET={first_target["target"]}+0x{entry["offset"]:x}')
        print(f'QEMU_WATCH_FIRST_TARGET_PC=0x{entry["pc"]:x}')
        print(f'QEMU_WATCH_FIRST_TARGET_CONTEXT={" -> ".join(context_names[-24:])}')
    if stall:
        context_names = [entry["symbol"] for entry in stall["context"]]
        print(f'QEMU_WATCH_STALL_LOOP={",".join(stall["loop_symbols"])}')
        print(f'QEMU_WATCH_STALL_SUFFIX_ENTRIES={stall["suffix_entries"]}')
        print(f'QEMU_WATCH_STALL_CONTEXT={" -> ".join(context_names[-12:])}')
    if hints:
        print("QEMU_WATCH_CONSOLE_HINTS_BEGIN")
        for line in hints:
            print(line)
        print("QEMU_WATCH_CONSOLE_HINTS_END")
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
        "trace_tail_symbols": tail_symbols,
        "trace_first_target": first_target,
        "trace_stall": stall,
        "console_hints": hints,
    }, indent=2, sort_keys=True) + "\n")

    return classifier_rc


if __name__ == "__main__":
    raise SystemExit(main())
