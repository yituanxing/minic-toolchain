#!/usr/bin/env python3
"""Check decoded machine words, not assembly pseudo-instruction spellings.

An RV64I .s file may legally contain li/mv/call/ret. This verifier runs
*after* GNU AS and GNU LD and independently checks the emitted 32-bit
encodings, soft-float ELF ABI and recorded ISA attribute. It intentionally
accepts no compressed, M, A, F/D, Zicsr or Zifencei instructions.

This is a strict RV64I *regression gate* for known code-only test objects
and executables, not a general ELF verifier for arbitrary .text data pools.
"""
from __future__ import annotations

import re
import subprocess
import sys


def rv64i(word: int) -> bool:
    """Only the RV64I base encodings; do not whitelist assembler aliases."""
    if (word & 3) != 3:  # no 16-bit C extension
        return False
    op = word & 0x7F
    f3 = (word >> 12) & 7
    f7 = (word >> 25) & 0x7F
    if op in (0x17, 0x37, 0x6F):  # AUIPC, LUI, JAL
        return True
    if op == 0x67:  # JALR
        return f3 == 0
    if op == 0x63:  # conditional branches
        return f3 in (0, 1, 4, 5, 6, 7)
    if op == 0x03:  # RV64 loads, but not FP loads
        return f3 in (0, 1, 2, 3, 4, 5, 6)
    if op == 0x23:  # stores
        return f3 in (0, 1, 2, 3)
    if op == 0x13:  # XLEN immediates
        if f3 == 1:
            return (word >> 26) & 0x3F == 0
        if f3 == 5:
            return ((word >> 26) & 0x3F) in (0, 0x10)
        return f3 in (0, 2, 3, 4, 6, 7)
    if op == 0x1B:  # RV64W immediates (shamt limited to five bits)
        if f3 == 0:
            return True
        if f3 == 1:
            return f7 == 0
        if f3 == 5:
            return f7 in (0, 0x20)
        return False
    if op == 0x33:  # XLEN registers: prohibit mul/div funct7=1
        return f7 == 0 or (f7 == 0x20 and f3 in (0, 5))
    if op == 0x3B:  # RV64W registers
        return (f7 == 0 and f3 in (0, 1, 5)) or (
            f7 == 0x20 and f3 in (0, 5)
        )
    if op == 0x0F:  # FENCE, but not FENCE.I or FENCE.TSO
        return f3 == 0 and (word >> 28) & 0xF == 0
    if op == 0x73:  # only ECALL and EBREAK; no CSR instructions
        return word in (0x00000073, 0x00100073)
    return False


def selftest() -> None:
    good = (
        0x00000013,  # addi x0,x0,0 (nop pseudo)
        0x00000513,  # addi a0,x0,0 (li pseudo)
        0x00008067,  # jalr x0,0(ra) (ret pseudo)
        0x0000006F,  # jal
        0x40005013,  # srai x0,x0,0 (RV64 immediate)
        0x00000073,  # ecall
        0x00100073,  # ebreak
        0x0000000F,  # fence
        0x0000001B,  # addiw
        0x0000003B,  # addw
    )
    bad = (
        0x00000001,  # C.nop
        0x02000033,  # mul
        0x02004033,  # div
        0x0000202F,  # amoadd.w
        0x02000053,  # F/D FP
        0xC0002073,  # CSRRS
        0x0000100F,  # fence.i
        0x8000000F,  # fence.tso (Ztso)
        0x00000000,  # invalid/unimp
        0x40001013,  # reserved slli encoding
    )
    for word in good:
        if not rv64i(word):
            raise RuntimeError(f"decoder rejected base I encoding 0x{word:08x}")
    for word in bad:
        if rv64i(word):
            raise RuntimeError(f"decoder accepted extension/reserved 0x{word:08x}")


def run(*args: str) -> str:
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def inspect(path: str) -> int:
    hdr = run("riscv64-linux-gnu-readelf", "-h", path)
    if not re.search(r"Machine:\s+RISC-V\b", hdr):
        raise RuntimeError(f"{path}: not RISC-V ELF")
    match = re.search(r"Flags:\s+0x([0-9a-fA-F]+)", hdr)
    if match is None or int(match.group(1), 16) & 0xF:
        raise RuntimeError(f"{path}: ELF flags not soft-float, no-RVC, RV64I")
    attrs = run("riscv64-linux-gnu-readelf", "-A", path)
    arch_tags = re.findall(r'Tag_RISCV_arch:\s*"([^"]+)"', attrs)
    if not arch_tags or any(
        re.fullmatch(r"rv64i[0-9]+p[0-9]+", tag) is None for tag in arch_tags
    ):
        raise RuntimeError(f"{path}: ISA attributes not all RV64I: {arch_tags}")
    dis = run("riscv64-linux-gnu-objdump", "-d", "-z", "-M", "no-aliases", path)
    instructions = 0
    for line in dis.splitlines():
        match = re.match(
            r"^\s*([0-9a-fA-F]+):\s+([0-9a-fA-F]{4,8})\s+(\S+)", line
        )
        if match is None:
            continue
        location, encoding, mnemonic = match.groups()
        if len(encoding) != 8:
            raise RuntimeError(
                f"{path}: compressed/unqualified encoding 0x{encoding} at {location}"
            )
        word = int(encoding, 16)
        if not rv64i(word):
            raise RuntimeError(
                f"{path}: non-RV64I machine word 0x{word:08x} "
                f"at {location} ({mnemonic})"
            )
        instructions += 1
    if instructions == 0:
        raise RuntimeError(f"{path}: no decoded executable instructions")
    return instructions


def main() -> None:
    selftest()
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        print("RV64I_MACHINE_DECODER=PASS positive=10 negative=10")
        return
    if len(sys.argv) < 2:
        raise RuntimeError("usage: audit-rv64i-machine.py ELF [ELF ...]")
    total = 0
    for path in sys.argv[1:]:
        count = inspect(path)
        total += count
        print(f"RV64I_MACHINE_FILE=PASS instructions={count} path={path}")
    print(f"RV64I_MACHINE_AUDIT=PASS files={len(sys.argv)-1} instructions={total} "
          "no_aliases=1 strict_arch_attribute=1 soft_float=1")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
        print(f"RV64I_MACHINE_AUDIT=FAIL {error}", file=sys.stderr)
        sys.exit(1)
