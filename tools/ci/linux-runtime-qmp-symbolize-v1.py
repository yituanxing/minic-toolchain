#!/usr/bin/env python3
"""Resolve read-only RV64 QMP PC/exception snapshots against same-trial vmlinux.

Addresses outside linked Linux text are reported as outside, never evidence
that Linux executed. This script does not alter the Image or boot verdict.
"""
from __future__ import annotations

import argparse
import bisect
from collections import Counter
import json
from pathlib import Path
import subprocess


def symbolize(samples_text: str, nm_text: str) -> list[str]:
    lines: list[str] = []
    pairs: list[tuple[int, str]] = []
    layout: dict[str, int] = {}
    for row in nm_text.splitlines():
        fields = row.split(None, 2)
        if len(fields) != 3:
            continue
        try:
            addr = int(fields[0], 16)
        except ValueError:
            continue
        if fields[1] in {"t", "T", "w", "W"}:
            pairs.append((addr, fields[2]))
        if fields[2] in ("__earlycon_table", "__earlycon_table_end"):
            layout[fields[2]] = addr
    pairs.sort()
    addresses = [address for address, _ in pairs]
    if len(layout) == 2:
        start, end = layout["__earlycon_table"], layout["__earlycon_table_end"]
        lines.append(f"COHORT_EARLYCON_TABLE_LAYOUT begin=0x{start:x} end=0x{end:x} "
                     f"span_bytes={end-start} struct_earlycon_id_size_expected=152 "
                     f"remainder={(end-start)%152}")
    else:
        lines.append("COHORT_EARLYCON_TABLE_LAYOUT=INCOMPLETE names=" +
                     ",".join(sorted(layout)))
    pcs: list[int] = []
    pc_symbols: list[tuple[int, str]] = []
    for row in samples_text.splitlines():
        sample = json.loads(row)
        at = sample["at_seconds"]
        registers = sample.get("registers", {})
        for register in ("pc", "mepc", "sepc"):
            raw = registers.get(register)
            if not raw or not addresses:
                continue
            try:
                addr = int(raw, 16)
            except ValueError:
                continue
            if register == "pc":
                pcs.append(addr)
            index = bisect.bisect_right(addresses, addr) - 1
            if index < 0:
                lines.append(f"COHORT_QMP_SYMBOL at_s={at} reg={register} "
                             f"addr=0x{addr:x} symbol=BELOW_LINKED_TEXT")
            else:
                base, symbol = pairs[index]
                if register == "pc":
                    pc_symbols.append((at, symbol))
                lines.append(f"COHORT_QMP_SYMBOL at_s={at} reg={register} "
                             f"addr=0x{addr:x} symbol={symbol} offset=0x{addr-base:x}")
    if len(pcs) >= 2 and pcs[-1] == pcs[-2]:
        lines.append(f"COHORT_QMP_STABLE_PC=OBSERVED samples={len(pcs)} "
                     f"addr=0x{pcs[-1]:x}")
    frequency = Counter(symbol for _, symbol in pc_symbols)
    lines.append(f"COHORT_QMP_PC_DIVERSITY samples={len(pcs)} "
                 f"unique_pcs={len(set(pcs))} unique_functions={len(frequency)} "
                 f"last_symbol={pc_symbols[-1][1] if pc_symbols else 'UNAVAILABLE'}")
    for symbol, count in frequency.most_common(12):
        lines.append(f"COHORT_QMP_HOTSPOT samples={count} symbol={symbol}")
    return lines


def self_test() -> None:
    nm = "ffffffff80001000 T linux_entry\nffffffff80002000 t later\n" \
         "ffffffff81000000 R __earlycon_table\nffffffff81000130 R __earlycon_table_end\n"
    samples = '\n'.join((
        json.dumps({"at_seconds": 1, "registers": {"pc": "0000000080200000"}}),
        json.dumps({"at_seconds": 4, "registers": {"pc": "ffffffff80001008",
                                                    "sepc": "ffffffff80002003"}}),
        json.dumps({"at_seconds": 8, "registers": {"pc": "ffffffff80001008"}}),
    ))
    lines = symbolize(samples, nm)
    assert any("symbol=BELOW_LINKED_TEXT" in line for line in lines)
    assert any("symbol=linux_entry offset=0x8" in line for line in lines)
    assert any("reg=sepc" in line and "symbol=later offset=0x3" in line for line in lines)
    assert any("COHORT_QMP_STABLE_PC=OBSERVED" in line for line in lines)
    assert any("remainder=0" in line for line in lines)
    print("LINUX_RUNTIME_QMP_SYMBOLIZE_SELFTEST=PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--samples", type=Path)
    parser.add_argument("--vmlinux", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.samples is None or args.vmlinux is None:
        parser.error("--samples and --vmlinux are required")
    if not args.samples.is_file() or not args.vmlinux.is_file():
        parser.error("missing samples or same-trial vmlinux")
    nm = subprocess.run(["/usr/bin/riscv64-linux-gnu-nm", "-n", "--defined-only",
                         str(args.vmlinux)], text=True, capture_output=True, check=False)
    if nm.returncode:
        print(f"COHORT_QMP_SYMBOL=INCONCLUSIVE nm_rc={nm.returncode}")
        return 0
    for line in symbolize(args.samples.read_text(), nm.stdout):
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
