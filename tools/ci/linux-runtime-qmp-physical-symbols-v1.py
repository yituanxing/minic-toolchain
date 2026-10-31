#!/usr/bin/env python3
"""Bounded, read-only physical-to-linked RV64 QMP fault evidence.

An early Linux trap runs with satp=0, so the PC is physical while GNU nm
reports vmlinux's linked virtual addresses. Never compare them directly.
No diagnosis here changes Image, Kbuild objects, or a P0/P1 verdict.
"""
from __future__ import annotations

import argparse
import bisect
import gzip
import json
from pathlib import Path
import struct
import subprocess
import tempfile


DRAM_BASE = 0x80000000  # QEMU virt RAM, fixed by this campaign's loader.
MAX_NM_GZIP = 8 * 1024 * 1024
MAX_DISASSEMBLY = 48 * 1024


def image_physical_layout(image: Path) -> tuple[int, int]:
    with image.open("rb") as stream:
        header = stream.read(64)
    if len(header) != 64 or header[48:56] != b"RISCV\0\0\0":
        raise ValueError("unrecognized RISC-V Linux Image header")
    text_offset, header_size = struct.unpack_from("<QQ", header, 8)
    actual_size = image.stat().st_size
    if text_offset > 128 * 1024 * 1024 or header_size != actual_size:
        raise ValueError("Image header size/offset mismatch")
    return DRAM_BASE + text_offset, actual_size


def parse_nm(nm_text: str) -> tuple[int | None, list[tuple[int, str]]]:
    linked_start: int | None = None
    names: list[tuple[int, str]] = []
    for row in nm_text.splitlines():
        fields = row.split(None, 2)
        if len(fields) != 3:
            continue
        try:
            address = int(fields[0], 16)
        except ValueError:
            continue
        if fields[2] == "_start" and fields[1] in ("T", "t"):
            linked_start = address
        if fields[1] in ("T", "t"):
            names.append((address, fields[2]))
    names.sort()
    return linked_start, names


def resolve(samples: str, nm_text: str, image_base: int,
            image_size: int) -> tuple[list[str], list[tuple[str, int]]]:
    start, names = parse_nm(nm_text)
    if start is None or not names:
        return ["COHORT_QMP_PHYSICAL_SYMBOLS=UNAVAILABLE reason=linked_start_missing"], []
    addresses = [addr for addr, _ in names]
    lines = [f"COHORT_QMP_LOAD_BIAS=PASS physical_start=0x{image_base:x} "
             f"linked_start=0x{start:x} image_bytes={image_size} "
             f"linked_minus_physical=0x{(start-image_base) & ((1 << 64)-1):x}"]
    windows: dict[int, str] = {}
    unique_samples: set[tuple[str, int]] = set()
    for row in samples.splitlines():
        try:
            sample = json.loads(row)
            at = sample["at_seconds"]
            regs = sample["registers"]
        except (ValueError, KeyError, TypeError):
            lines.append("COHORT_QMP_PHYSICAL_SYMBOLS=UNAVAILABLE reason=sample_malformed")
            continue
        for register in ("pc", "mepc", "sepc", "ra"):
            if register not in regs:
                continue
            try:
                physical = int(regs[register], 16)
            except (TypeError, ValueError):
                continue
            if not image_base <= physical < image_base + image_size:
                lines.append(f"COHORT_QMP_PHYSICAL_SYMBOL at_s={at} reg={register} "
                             f"physical=0x{physical:x} status=OUTSIDE_IMAGE")
                continue
            linked = (start + physical - image_base) & ((1 << 64)-1)
            index = bisect.bisect_right(addresses, linked) - 1
            if index < 0:
                lines.append(f"COHORT_QMP_PHYSICAL_SYMBOL at_s={at} reg={register} "
                             f"physical=0x{physical:x} linked=0x{linked:x} "
                             "status=BELOW_TEXT")
                continue
            symbol_at, symbol = names[index]
            lines.append(f"COHORT_QMP_PHYSICAL_SYMBOL at_s={at} reg={register} "
                         f"physical=0x{physical:x} linked=0x{linked:x} "
                         f"symbol={symbol} offset=0x{linked-symbol_at:x}")
            if register in ("sepc", "ra") and (register, linked) not in unique_samples:
                unique_samples.add((register, linked))
                windows[linked] = register
        if "mtval" in regs:
            try:
                fault = int(regs["mtval"], 16)
            except (ValueError, TypeError):
                continue
            low32 = fault & 0xffffffff
            sign_extended = (low32 | (0xffffffff00000000 if low32 & 0x80000000 else 0))
            if fault == sign_extended and fault != low32 and \
                    image_base <= low32 < image_base + image_size:
                lines.append(f"COHORT_QMP_SIGN_EXTENDED_ADDR_CANDIDATE at_s={at} "
                             f"fault=0x{fault:x} low32=0x{low32:x} "
                             "proof=PATTERN_ONLY")
    return lines, [(label, address) for address, label in windows.items()]


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="minic-rv64-qmp-physical-") as dirname:
        candidate = Path(dirname) / "Image"
        header = bytearray(128)
        struct.pack_into("<QQ", header, 8, 0x200000, len(header))
        header[48:56] = b"RISCV\0\0\0"
        candidate.write_bytes(header)
        assert image_physical_layout(candidate) == (0x80200000, 128)
        header[48:56] = b"INVALID!"
        candidate.write_bytes(header)
        try:
            image_physical_layout(candidate)
        except ValueError:
            pass
        else:
            raise AssertionError("non-Linux image must fail closed")
    nm = ("ffffffff80000000 T _start\n"
          "ffffffff80001000 t trap_park\n"
          "ffffffff80800000 T actual_load\n"
          "ffffffff80830000 T call_owner\n")
    base = 0x80200000
    samples = json.dumps({"at_seconds": 1, "registers": {
        "pc": "0000000080201004", "sepc": "0000000080a0000a",
        "ra": "0000000080a30004", "mtval": "ffffffff80a10004"}})
    lines, windows = resolve(samples, nm, base, 0x1000000)
    joined = "\n".join(lines)
    assert "reg=sepc physical=0x80a0000a linked=0xffffffff8080000a " \
           "symbol=actual_load offset=0xa" in joined
    assert "reg=ra physical=0x80a30004 linked=0xffffffff80830004 " \
           "symbol=call_owner offset=0x4" in joined
    assert "reg=pc physical=0x80201004 linked=0xffffffff80001004 " \
           "symbol=trap_park" in joined
    assert "COHORT_QMP_SIGN_EXTENDED_ADDR_CANDIDATE" in joined
    assert len(windows) == 2
    absent, no_windows = resolve(samples, "", base, 0x1000000)
    assert absent[0].startswith("COHORT_QMP_PHYSICAL_SYMBOLS=UNAVAILABLE")
    assert not no_windows
    out_of_image, _ = resolve(samples, nm, base, 0x1000)
    assert "status=OUTSIDE_IMAGE" in "\n".join(out_of_image)
    print("LINUX_RUNTIME_QMP_PHYSICAL_SYMBOLS_SELFTEST=PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--samples", type=Path)
    parser.add_argument("--vmlinux", type=Path)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if any(arg is None for arg in (args.samples, args.vmlinux,
                                    args.image, args.output_dir)):
        parser.error("--samples, --vmlinux, --image, --output-dir are required")
    if not (args.samples.is_file() and args.vmlinux.is_file() and
            args.image.is_file() and args.output_dir.is_dir()):
        print("COHORT_QMP_PHYSICAL_SYMBOLS=UNAVAILABLE reason=input_missing")
        return 0
    try:
        base, size = image_physical_layout(args.image)
    except (OSError, ValueError) as error:
        print(f"COHORT_QMP_PHYSICAL_SYMBOLS=UNAVAILABLE reason={error}")
        return 0
    try:
        nm = subprocess.run(["/usr/bin/riscv64-linux-gnu-nm", "-n",
                             "--defined-only", str(args.vmlinux)],
                            text=True, capture_output=True, timeout=25,
                            check=False)
    except (OSError, subprocess.TimeoutExpired):
        print("COHORT_QMP_PHYSICAL_SYMBOLS=UNAVAILABLE reason=nm_failed")
        return 0
    if nm.returncode:
        print(f"COHORT_QMP_PHYSICAL_SYMBOLS=UNAVAILABLE reason=nm_rc_{nm.returncode}")
        return 0
    packed = gzip.compress(nm.stdout.encode("utf-8"), compresslevel=9)
    if len(packed) <= MAX_NM_GZIP:
        (args.output_dir / "qmp-physical-vmlinux-nm.txt.gz").write_bytes(packed)
        print(f"COHORT_QMP_NM_ARCHIVE=PASS compressed_bytes={len(packed)}")
    else:
        print(f"COHORT_QMP_NM_ARCHIVE=SKIPPED reason=size_bound bytes={len(packed)}")
    lines, windows = resolve(args.samples.read_text(), nm.stdout, base, size)
    for line in lines:
        print(line)
    assembler: list[str] = []
    for register, linked in windows[:4]:
        lo = max(0, linked - 48)
        hi = min((1 << 64)-1, linked + 64)
        try:
            probe = subprocess.run([
                "/usr/bin/riscv64-linux-gnu-objdump", "-d",
                f"--start-address=0x{lo:x}", f"--stop-address=0x{hi:x}",
                str(args.vmlinux)], text=True, capture_output=True,
                timeout=15, check=False)
        except (OSError, subprocess.TimeoutExpired):
            assembler.append(f"COHORT_QMP_DISASM=UNAVAILABLE reg={register} reason=tool_failed")
            continue
        assembler.append(f"COHORT_QMP_DISASM_WINDOW reg={register} linked=0x{linked:x} "
                         f"start=0x{lo:x} end=0x{hi:x} rc={probe.returncode}")
        assembler.append(probe.stdout[:8192])
    if assembler:
        dump = "\n".join(assembler)[:MAX_DISASSEMBLY]
        (args.output_dir / "qmp-physical-disasm.log").write_text(dump)
        print(f"COHORT_QMP_DISASM=DIAGNOSTIC_ONLY windows={min(4, len(windows))} "
              f"bytes={len(dump)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
