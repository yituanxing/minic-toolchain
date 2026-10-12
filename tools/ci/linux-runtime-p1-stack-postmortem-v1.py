#!/usr/bin/env python3
"""Offline symbolization of frozen P1 first-fault registers and stack words.

This is NOT a verified frame unwind. Stack words merely resembling text
return addresses are labelled candidates, never declared a call chain.
"""
from __future__ import annotations
import argparse
import bisect
from pathlib import Path
import re
import struct

FRAME = re.compile(
    r"P1_GDB_FRAME name=(\S+) pc=(0x[\da-fA-F]+) sp=(0x[\da-fA-F]+) "
    r"ra=(0x[\da-fA-F]+) .*"
)
FAULT = re.compile(r"P1_FIRSTFAULT\s+([^\n]+)")
KEY = re.compile(r"(\w+)=(0x[\da-fA-F]+)")
IRQ = re.compile(r"P1_GDB_IRQ_LAYOUT irq_base=(0x[\da-fA-F]+) irq_top=(0x[\da-fA-F]+)")
MAX_SYMBOL_OFFSET = 8192


def load_symbols(path: Path):
    pairs = []
    for raw in path.read_text(errors="replace").splitlines():
        p = raw.split(None, 2)
        if len(p) == 3 and p[1] in ("T", "t", "W", "w"):
            try:
                pairs.append((int(p[0], 16), p[2]))
            except ValueError:
                pass
    # Some aliases share an address. Sorting the stable name is sufficient
    # for diagnostic attribution, not for a confirmed DWARF frame unwind.
    pairs.sort()
    return [x[0] for x in pairs], pairs


def symbol_of(addr: int, addresses, symbols):
    i = bisect.bisect_right(addresses, addr) - 1
    if i < 0:
        return None
    sym_start, sym_name = symbols[i]
    if addr - sym_start > MAX_SYMBOL_OFFSET:
        return None
    return f"{sym_name}+0x{addr-sym_start:x}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dir", type=Path, required=True)
    args = parser.parse_args()
    dest = args.dir
    log = (dest / "gdb.log").read_text(errors="replace")
    addresses, symbols = load_symbols(dest / "linked-symbols.nm")
    fault_matches = FAULT.findall(log)
    if not fault_matches:
        print("P1_POSTMORTEM=INCONCLUSIVE no_firstfault_capture")
        return 0
    last_fault = dict(KEY.findall(fault_matches[-1]))
    sp = int(last_fault["sp"], 16)
    epc = int(last_fault["epc"], 16)
    ra = int(last_fault["ra"], 16)
    irq_lo = int(last_fault["irq_base"], 16)
    irq_hi = int(last_fault["irq_top"], 16)
    task_lo = int(last_fault["task_lo"], 16)
    if task_lo - 4096 <= sp < task_lo + 16384:
        active = "task"
        below = max(0, task_lo - sp)
    elif irq_lo - 4096 <= sp < irq_hi:
        active = "irq"
        below = max(0, irq_lo - sp)
    else:
        active = "unverified"
        below = -1
    epc_name = symbol_of(epc, addresses, symbols) or "UNKNOWN"
    ra_name = symbol_of(ra, addresses, symbols) or "UNKNOWN"
    print(f"P1_POSTMORTEM=FIRSTFAULT stack={active} below_base={below} "
          f"epc={epc_name} ra={ra_name} fault_sp=0x{sp:x}")
    lines = [
        "type\tstack\tsp_or_slot\tpc_or_return_address\tlinked_symbol\tremaining_bytes\tconfidence"
    ]
    frames = FRAME.findall(log)
    # Existing breakpoint events occur at different times and even different
    # tasks. Never present all consecutive rows as a single caller chain.
    for name, pc, frame_sp, frame_ra in frames[-350:]:
        ptr = int(frame_sp, 16)
        if irq_lo <= ptr < irq_hi:
            stack, remain = "irq", ptr - irq_lo
        elif task_lo <= ptr < task_lo + 16384:
            stack, remain = "task", ptr - task_lo
        else:
            stack, remain = "other_task_or_unknown", -1
        ra_addr = int(frame_ra, 16)
        lines.append(f"observed_entry\t{stack}\t{frame_sp}\t{frame_ra}\t"
                     f"{symbol_of(ra_addr,addresses,symbols) or 'UNKNOWN'}"
                     f"\t{remain}\tentry_only")
    stack_candidates = {}
    for stack_name, base in (("irq", irq_lo), ("task", task_lo)):
        blob = dest / (stack_name + "-stack.bin")
        if not blob.exists():
            print(f"P1_POSTMORTEM_STACK=INCONCLUSIVE stack={stack_name} missing_binary")
            continue
        data = blob.read_bytes()
        if len(data) != 16384:
            print(f"P1_POSTMORTEM_STACK=INCONCLUSIVE stack={stack_name} "
                  f"incorrect_snapshot_bytes={len(data)}")
            continue
        candidates = []
        for offset in range(0, len(data), 8):
            value = struct.unpack_from("<Q", data, offset)[0]
            sym = symbol_of(value, addresses, symbols)
            if sym:
                candidates.append((offset, value, sym))
                lines.append(f"stack_word_candidate\t{stack_name}\t0x{base+offset:x}"
                             f"\t0x{value:x}\t{sym}\t{offset}"
                             f"\tNOT_VERIFIED_AS_RETURN_ADDRESS")
        stack_candidates[stack_name] = candidates
        print(f"P1_POSTMORTEM_STACK=CAPTURED stack={stack_name} "
              f"bytes={len(data)} code_pointer_candidates={len(candidates)}")
    (dest / "postmortem.tsv").write_text("\n".join(lines)+"\n")
    (dest / "firstfault-summary.txt").write_text(
        f"stack={active}\nsp=0x{sp:x}\nstack_below_base={below}\n"
        f"epc=0x{epc:x} {epc_name}\nra=0x{ra:x} {ra_name}\n"
        f"irq=[0x{irq_lo:x},0x{irq_hi:x})\n"
        f"tcp_task=[0x{task_lo:x},0x{task_lo+16384:x})\n"
        "Stack word matches are not a proven call chain.\n"
    )
    print(f"P1_POSTMORTEM=CAPTURED frame_entries={len(frames)} "
          f"snapshot_stacks={len(stack_candidates)}")
    for offset, ptr, sym in stack_candidates.get(active, [])[:32]:
        print(f"P1_POSTMORTEM_CANDIDATE stack={active} offset={offset} "
              f"code=0x{ptr:x} symbol={sym}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
