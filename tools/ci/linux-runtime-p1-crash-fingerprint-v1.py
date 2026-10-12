#!/usr/bin/env python3
"""Report the actual fault frontier of a frozen Linux P1 object-swap trial.

FAIL at a different EPC is evidence of progress, not proof of identical failure.
Do not confuse stack exhaustion with a successful twelve-syscall P1 oracle.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re

STACK = re.compile(r"Task stack:\s*\[(?:0x)?([0-9a-fA-F]+)\.\.(?:0x)?([0-9a-fA-F]+)\]")
SP = re.compile(r"\bsp\s*:\s*(?:0x)?([0-9a-fA-F]{8,16})\b")
EPC = re.compile(r"\bepc\s*:\s*([\w.$]+)\+0x([0-9a-fA-F]+)/0x([0-9a-fA-F]+)")
RA = re.compile(r"\bra\s*:\s*([\w.$]+)\+0x([0-9a-fA-F]+)/0x([0-9a-fA-F]+)")


def classify(log: str) -> dict[str, str]:
    if "Insufficient stack space to handle exception!" in log:
        category = "STACK_OVERFLOW"
    elif "Kernel panic" in log or "Oops:" in log:
        category = "OTHER_KERNEL_CRASH"
    else:
        category = "NO_KERNEL_CRASH_MARKER"
    marker = log.find("Insufficient stack space to handle exception!")
    excerpt = log[marker:] if marker >= 0 else log
    task, epc, ra, sp = (r.search(excerpt) for r in (STACK, EPC, RA, SP))
    entry = {
        "category": category,
        "epc": epc.group(1) if epc else "UNKNOWN",
        "epc_offset": "0x" + epc.group(2) if epc else "-",
        "ra": ra.group(1) if ra else "UNKNOWN",
        "stack_lo": "0x" + task.group(1) if task else "-",
        "sp": "0x" + sp.group(1) if sp else "-",
        "sp_below_stack_bytes": "-",
    }
    if task and sp:
        low = int(task.group(1), 16)
        value = int(sp.group(1), 16)
        entry["sp_below_stack_bytes"] = str(max(0, low - value))
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
