#!/usr/bin/env python3
import argparse
import bisect
import re
from pathlib import Path


def parse_int(value: str) -> int:
    return int(value, 0)


def load_symbols(path: Path):
    symbols = []
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        try:
            address = int(parts[0], 16)
        except ValueError:
            continue
        symbols.append((address, parts[2]))
    symbols.sort()
    return symbols


def resolve(symbols, addresses, address):
    index = bisect.bisect_right(addresses, address) - 1
    if index < 0:
        return "?", 0
    base, name = symbols[index]
    return name, address - base


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Summarize the first RISC-V Linux runtime fault with early-boot relocation-aware symbols."
    )
    parser.add_argument("--interrupts", required=True, type=Path)
    parser.add_argument("--nm", required=True, type=Path)
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--console", required=True, type=Path)
    parser.add_argument("--qemu-rc", required=True)
    parser.add_argument("--phys-entry", type=parse_int, default=0x80200000)
    args = parser.parse_args()

    symbols = load_symbols(args.nm)
    addresses = [address for address, _ in symbols]
    by_name = {name: address for address, name in symbols}
    trace = args.trace.read_text(errors="replace")
    console = args.console.read_text(errors="replace")
    interrupt_lines = args.interrupts.read_text(errors="replace").splitlines()

    anchor_name = None
    anchor_address = None
    for candidate in ("_start", "_text", "_stext"):
        if candidate in by_name:
            anchor_name = candidate
            anchor_address = by_name[candidate]
            break

    relocation_delta = None
    if anchor_address is not None and anchor_address > args.phys_entry:
        relocation_delta = anchor_address - args.phys_entry

    faults = []
    pattern = re.compile(
        r"epc:0x([0-9a-fA-F]+).*tval:0x([0-9a-fA-F]+).*desc=([^ ]+)"
    )
    for line in interrupt_lines:
        if "page_fault" not in line:
            continue
        match = pattern.search(line)
        if not match:
            continue
        epc = int(match.group(1), 16)
        tval = int(match.group(2), 16)
        desc = match.group(3)
        lookup = epc
        relocated = False
        if relocation_delta is not None and epc < anchor_address:
            lookup = epc + relocation_delta
            relocated = True
        symbol, offset = resolve(symbols, addresses, lookup)
        faults.append((epc, tval, desc, lookup, relocated, symbol, offset))

    def executed(name: str) -> bool:
        address = by_name.get(name)
        if address is None:
            return False
        candidates = [address]
        if relocation_delta is not None and address >= relocation_delta:
            candidates.append(address - relocation_delta)
        return any(f"0x{candidate:016x}:" in trace for candidate in candidates)

    guard_faults = [
        fault
        for fault in faults
        if fault[2] in ("store_page_fault", "load_page_fault")
        and 0xFF1FFFFFFFF00000 <= fault[1] < 0xFF20000000000000
    ]
    null_faults = [fault for fault in faults if fault[1] == 0]
    exec_faults = [fault for fault in faults if fault[2] == "exec_page_fault"]
    bad_stack = executed("handle_kernel_stack_overflow") or executed("handle_bad_stack")
    panic = executed("panic") or "Kernel panic" in console
    banner = "Linux version" in console

    print(f"QEMU_RC={args.qemu_rc}")
    print(f"PAGE_FAULTS={len(faults)}")
    print(f"EXEC_PAGE_FAULTS={len(exec_faults)}")
    print(f"IRQ_GUARD_RANGE_FAULTS={len(guard_faults)}")
    print(f"NULL_PAGE_FAULTS={len(null_faults)}")
    print(f"BAD_STACK_PATH={1 if bad_stack else 0}")
    print(f"PANIC_PATH={1 if panic else 0}")
    print(f"LINUX_BANNER={1 if banner else 0}")
    if anchor_name is not None:
        print(f"LINK_ANCHOR={anchor_name}@0x{anchor_address:016x}")
    if relocation_delta is not None:
        print(f"EARLY_PHYS_TO_LINK_DELTA=0x{relocation_delta:x}")

    for index, fault in enumerate(faults[:12]):
        epc, tval, desc, lookup, relocated, symbol, offset = fault
        print(
            "FAULT"
            f" index={index} desc={desc}"
            f" epc=0x{epc:016x} tval=0x{tval:016x}"
            f" lookup=0x{lookup:016x} relocated={1 if relocated else 0}"
            f" symbol={symbol}+0x{offset:x}"
        )

    if guard_faults or bad_stack or panic or (exec_faults and not banner):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
