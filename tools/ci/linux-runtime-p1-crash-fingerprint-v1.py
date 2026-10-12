#!/usr/bin/env python3
"""Report the actual fault frontier of a frozen Linux P1 object-swap trial.

FAIL at a different EPC is evidence of progress, not proof of identical failure.
Do not confuse stack exhaustion with a successful twelve-syscall P1 oracle.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re

STACK = re.compile(r"Task stack:\\s*\\[(?:0x)?([0-9a-fA-F]+)\\.\\.(?:0x)?([0-9a-fA-F]+)\\]")
OVERFLOW = re.compile(r"Overflow stack:\\s*\\[(?:0x)?([0-9a-fA-F]+)\\.\\.(?:0x)?([0-9a-fA-F]+)\\]")
VMALLOC = re.compile(r"\\bvmalloc\\s*:\\s*(?:0x)?([0-9a-fA-F]+)\\s*-\\s*(?:0x)?([0-9a-fA-F]+)")
SP = re.compile(r"\\bsp\\s*:\\s*(?:0x)?([0-9a-fA-F]{8,16})\\b")
EPC = re.compile(r"\\bepc\\s*:\\s*([\\w.$]+)\\+0x([0-9a-fA-F]+)/0x([0-9a-fA-F]+)")
RA = re.compile(r"\\bra\\s*:\\s*([\\w.$]+)\\+0x([0-9a-fA-F]+)/0x([0-9a-fA-F]+)")


def classify(log: str) -> dict[str, str]:
    """Classify the trapped SP, not the emergency handler's running SP.

    Linux RISC-V handle_bad_stack prints current->stack, even when the
    offending SP belonged to a separate per-CPU IRQ stack. Therefore
    subtracting the task stack lower bound from every SP is invalid.
    """
    if "Insufficient stack space to handle exception!" in log:
        category = "STACK_OVERFLOW"
    elif "Kernel panic" in log or "Oops:" in log:
        category = "OTHER_KERNEL_CRASH"
    else:
        category = "NO_KERNEL_CRASH_MARKER"
    marker = log.find("Insufficient stack space to handle exception!")
    excerpt = log[marker:] if marker >= 0 else log
    task, overflow, epc, ra, sp = (
        r.search(excerpt) for r in (STACK, OVERFLOW, EPC, RA, SP)
    )
    vmalloc = VMALLOC.search(log)
    entry = {
        "category": category,
        "epc": epc.group(1) if epc else "UNKNOWN",
        "epc_offset": "0x" + epc.group(2) if epc else "-",
        "ra": ra.group(1) if ra else "UNKNOWN",
        "stack_lo": "0x" + task.group(1) if task else "-",
        "sp": "0x" + sp.group(1) if sp else "-",
        "sp_below_stack_bytes": "-",
        "sp_location": "UNKNOWN",
        "vmalloc_start": "0x" + vmalloc.group(1) if vmalloc else "-",
    }
    if sp:
        value = int(sp.group(1), 16)
        if task:
            low, high = int(task.group(1), 16), int(task.group(2), 16)
            if low <= value < high:
                entry["sp_location"] = "TASK_STACK"
            elif low - 4096 <= value < low:
                entry["sp_location"] = "TASK_STACK_NEAR_UNDERFLOW"
                entry["sp_below_stack_bytes"] = str(low - value)
        if overflow:
            low, high = int(overflow.group(1), 16), int(overflow.group(2), 16)
            if low <= value < high:
                entry["sp_location"] = "OVERFLOW_STACK"
        if entry["sp_location"] == "UNKNOWN" and vmalloc:
            start = int(vmalloc.group(1), 16)
            if start - 4096 <= value < start:
                # A candidate, NOT proof that this is the per-CPU IRQ
                # stack. GDB must independently establish irq_stack_ptr.
                entry["sp_location"] = "VMALLOC_START_NEAR_UNDERFLOW_CANDIDATE"
        if entry["sp_location"] == "UNKNOWN":
            entry["sp_location"] = "OTHER_OR_UNVERIFIED"
    return entry


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--trial", required=True)
    args = parser.parse_args()
    record = classify(args.log.read_text(encoding="utf-8", errors="replace"))
    print("COHORT_P1_CRASH_FINGERPRINT trial=" + args.trial + " " +
          " ".join("%s=%s" % (key, value) for key, value in record.items()),
          flush=True)


if __name__ == "__main__":
    main()
