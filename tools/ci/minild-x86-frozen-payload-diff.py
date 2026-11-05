#!/usr/bin/env python3
"""Non-gating same-producer GNU/MiniLD x86 Linux binary and relocs diff.

Linux's compressed payload contains a stripped *ELF* plus x86 postlink
relocations. A changed gzip size by itself cannot identify which producer
changed, or whether allocated instructions, ELF metadata, or relocations differ.
Do not use a different GNU Kbuild run as a byte-for-byte oracle.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
import struct
import subprocess
import sys


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def first_difference(left: bytes, right: bytes) -> int | None:
    for index in range(min(len(left), len(right))):
        if left[index] != right[index]:
            return index
    return min(len(left), len(right)) if len(left) != len(right) else None


def section_data(path: Path, payload: bytes) -> dict[str, tuple[int, int, int, str, bytes]]:
    listing = subprocess.check_output(["readelf", "-W", "-S", str(path)], text=True)
    results: dict[str, tuple[int, int, int, str, bytes]] = {}
    pattern = re.compile(
        r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([a-fA-F0-9]+)"
        r"\s+([a-fA-F0-9]+)\s+([a-fA-F0-9]+)\s+\S+\s+(\S+)"
    )
    for line in listing.splitlines():
        match = pattern.match(line)
        if match is None:
            continue
        name, kind, address, offset, length, flags = match.groups()
        if kind == "NOBITS":
            continue
        off, size = int(offset, 16), int(length, 16)
        if off + size > len(payload):
            raise ValueError(f"truncated ELF section {path}:{name}")
        results[name] = (int(address, 16), off, size, flags, payload[off : off + size])
    return results


def compare_elf(gnu_path: Path, mini_path: Path) -> None:
    gnu, mini = gnu_path.read_bytes(), mini_path.read_bytes()
    print(f"ELF GNU size={len(gnu)} sha256={digest(gnu)}")
    print(f"ELF MINI size={len(mini)} sha256={digest(mini)}")
    diff = first_difference(gnu, mini)
    print(f"ELF first_file_difference={diff if diff is not None else 'identical'}")
    original, rebuilt = section_data(gnu_path, gnu), section_data(mini_path, mini)
    for name in list(dict.fromkeys([*original, *rebuilt])):
        if name not in original or name not in rebuilt:
            print(f"ELF_SECTION_MISSING name={name} side={'gnu' if name not in original else 'mini'}")
            continue
        gaddr, goff, gsize, gflags, gdata = original[name]
        maddr, moff, msize, mflags, mdata = rebuilt[name]
        if gaddr == maddr and gsize == msize and gflags == mflags and gdata == mdata:
            continue
        index = first_difference(gdata, mdata)
        print(
            f"ELF_SECTION_DIFF name={name} gnu_addr={gaddr:#x} mini_addr={maddr:#x} "
            f"gnu_size={gsize} mini_size={msize} gnu_flags={gflags} mini_flags={mflags} "
            f"gnu_offset={goff:#x} mini_offset={moff:#x} "
            f"first_byte={index if index is not None else 'identical'} "
            f"gnu_sha256={digest(gdata)} mini_sha256={digest(mdata)}"
        )
        if index is not None:
            print(
                f"ELF_SECTION_WINDOW name={name} first_byte={index} "
                f"gnu={gdata[index : index + 16].hex()} "
                f"mini={mdata[index : index + 16].hex()}"
            )


def relocation_classes(raw: bytes) -> tuple[list[list[int]], list[int]]:
    if len(raw) % 4:
        raise ValueError(f"x86 relocation payload not uint32-aligned: {len(raw)}")
    values = list(struct.unpack(f"<{len(raw) // 4}I", raw))
    separators = [i for i, entry in enumerate(values) if entry == 0]
    if len(separators) != 3 or separators[0] != 0:
        print(f"RELOCS_UNEXPECTED_SENTINELS positions={separators[:12]} count={len(separators)}")
        return [], separators
    return (
        [values[1 : separators[1]],
         values[separators[1] + 1 : separators[2]],
         values[separators[2] + 1 :]],
        separators,
    )


def compare_relocs(gnu_path: Path, mini_path: Path) -> None:
    gnu, mini = gnu_path.read_bytes(), mini_path.read_bytes()
    print(f"RELOCS GNU size={len(gnu)} sha256={digest(gnu)}")
    print(f"RELOCS MINI size={len(mini)} sha256={digest(mini)}")
    index = first_difference(gnu, mini)
    print(
        f"RELOCS first_byte={index if index is not None else 'identical'} "
        f"gnu_window={(gnu[index : index + 32].hex() if index is not None else '')} "
        f"mini_window={(mini[index : index + 32].hex() if index is not None else '')}"
    )
    original, gnu_sep = relocation_classes(gnu)
    rebuilt, mini_sep = relocation_classes(mini)
    names = ("absolute64", "percpu32_negative", "absolute32")
    print(f"RELOCS sentinels GNU={gnu_sep} MINI={mini_sep}")
    if len(original) != 3 or len(rebuilt) != 3:
        return
    for name, glist, mlist in zip(names, original, rebuilt):
        index = next((i for i, (a, b) in enumerate(zip(glist, mlist)) if a != b), None)
        if index is None and len(glist) != len(mlist):
            index = min(len(glist), len(mlist))
        print(
            f"RELOCS_CLASS name={name} gnu_count={len(glist)} mini_count={len(mlist)} "
            f"first_item={index if index is not None else 'identical'} "
            f"gnu_sample={glist[max(0, (index or 0) - 2) : (index or 0) + 5]} "
            f"mini_sample={mlist[max(0, (index or 0) - 2) : (index or 0) + 5]}"
        )


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: diff.py GNU_STRIPPED_ELF MINI_STRIPPED_ELF GNU_RELOCS MINI_RELOCS", file=sys.stderr)
        return 2
    paths = [Path(path) for path in sys.argv[1:]]
    for path in paths:
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"missing binary provenance input: {path}")
    compare_elf(paths[0], paths[1])
    compare_relocs(paths[2], paths[3])
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"MINILD_X86_PAYLOAD_DIFF=ERROR {error}", file=sys.stderr)
        raise SystemExit(1)
