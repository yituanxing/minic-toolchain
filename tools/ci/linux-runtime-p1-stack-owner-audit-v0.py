#!/usr/bin/env python3
"""Identify exact MiniC/GNU C-object owners of the QEMU P1 IPv6 stack fault.

Source of truth: authenticated 2064 MiniC .o files restored from the SHA-
pinned GitHub Actions cache and independently QEMU-certified GNU .o pool.
No recompilation, linker or runtime change. This is attribution and frame
inspection only; a static observation is not proof of causality.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

NM = "riscv64-linux-gnu-nm"
OBJDUMP = "riscv64-linux-gnu-objdump"
SYMBOLS = (
    "__list_add_valid_or_report",
    "__minic_inline_spec_163_16339",
)
INTERESTING = (
    "lib/list_debug.o",
    "net/ipv6/tcp_ipv6.o",
    "net/ipv6/af_inet6.o",
    "net/ipv4/tcp.o",
    "net/ipv4/tcp_input.o",
    "net/ipv4/tcp_output.o",
    "net/ipv6/route.o",
    "net/core/sock.o",
    "net/core/skbuff.o",
)

def command(args: list[str]) -> str:
    proc = subprocess.run(args, capture_output=True, text=True, check=False)
    if proc.returncode:
        raise RuntimeError(f"{' '.join(args[:3])}: {proc.stderr[-500:]}")
    return proc.stdout

def capture_symbols(pool: Path, candidates: list[str]) -> dict[str, list[dict]]:
    found: dict[str, list[dict]] = {key: [] for key in SYMBOLS}
    for start in range(0, len(candidates), 128):
        names = candidates[start : start + 128]
        output = command([NM, "-A", "-S", "--defined-only"] +
                         [str(pool / name) for name in names])
        for line in output.splitlines():
            # /.../pool/net/ipv6/...o:00001234 00003450 t symbol
            match = re.search(
                r"^(.*?\.o):([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"
                r"([tTwW])\s+(\S+)$", line)
            if not match or match.group(5) not in found:
                continue
            obj = str(Path(match.group(1)).relative_to(pool))
            found[match.group(5)].append({
                "owner": obj, "offset": int(match.group(2), 16),
                "size": int(match.group(3), 16),
                "symbol": match.group(5), "binding": match.group(4),
            })
    return found

def disasm_window(obj: Path, symbol: str, max_lines: int = 56) -> list[str]:
    if not obj.is_file():
        return ["OBJECT_MISSING"]
    text = command([OBJDUMP, "-d", str(obj)])
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if line.rstrip().endswith(f"<{symbol}>:"):
            return lines[i : i + max_lines]
    return ["SYMBOL_NOT_IN_OBJECT"]

def frame_hints(lines: list[str]) -> list[str]:
    # Do not assert a full-stack bound based only on a prologue.
    matches = [s.strip() for s in lines if re.search(
        r"\b(addi\s+sp,sp,|sub\s+sp,sp,|c\.addi16sp|sd\s+ra,)", s)]
    return matches[:10]

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--minic", type=Path, required=True)
    parser.add_argument("--gcc", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    paths = [s.strip() for s in args.manifest.read_text().splitlines()
             if s.strip() and not s.lstrip().startswith("#")]
    assert len(paths) == 2064 and len(paths) == len(set(paths))
    for name in paths:
        assert (args.minic / name).is_file(), name
        assert (args.gcc / name).is_file(), name
    print(f"P1_OWNER_INPUT=PASS count={len(paths)} authentic_pools_expected=true")
    hits = capture_symbols(args.minic, paths)
    ghits = capture_symbols(args.gcc, paths)
    out: dict = {"minic": hits, "gcc": ghits, "objects": []}
    for symbol in SYMBOLS:
        for entry in hits[symbol]:
            print(f"P1_OWNER_SYMBOL compiler=MiniC symbol={symbol} "
                  f"owner={entry['owner']} size={entry['size']} "
                  f"offset=0x{entry['offset']:x}")
        for entry in ghits[symbol]:
            print(f"P1_OWNER_SYMBOL compiler=GNU symbol={symbol} "
                  f"owner={entry['owner']} size={entry['size']} "
                  f"offset=0x{entry['offset']:x}")
        assert hits[symbol], f"Missing crash-referenced symbol {symbol}"
    focus = sorted(set(INTERESTING) | {
        record["owner"] for values in hits.values() for record in values
    })
    for name in focus:
        if name not in paths:
            continue
        obj = args.minic / name
        golden = args.gcc / name
        msize, gsize = obj.stat().st_size, golden.stat().st_size
        out["objects"].append({"owner": name, "minic_bytes": msize,
                               "gcc_bytes": gsize, "ratio": round(msize/gsize, 3)})
        print(f"P1_OWNER_OBJECT owner={name} minic_bytes={msize} "
              f"gcc_bytes={gsize} ratio={msize/gsize:.3f}")
    for symbol in SYMBOLS:
        for entry in hits[symbol]:
            owner = entry["owner"]
            print(f"P1_OWNER_DISASM compiler=MiniC symbol={symbol} owner={owner}")
            window = disasm_window(args.minic / owner, symbol)
            print("\n".join(window[:30]))
            print("P1_OWNER_FRAME_HINTS=" + repr(frame_hints(window)))
            golden_candidates = [x for x in ghits[symbol] if x["owner"] == owner]
            if golden_candidates:
                print(f"P1_OWNER_DISASM compiler=GNU symbol={symbol} owner={owner}")
                gg = disasm_window(args.gcc / owner, symbol)
                print("\n".join(gg[:30]))
                print("P1_OWNER_GNU_FRAME_HINTS=" + repr(frame_hints(gg)))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2, sort_keys=True)+"\n")
    print(f"P1_OWNER_AUDIT=PASS output={args.out}")

if __name__ == "__main__":
    main()
