#!/usr/bin/env python3
"""Verify GNU/MiniLD Cortex-A53 843419 branch-island-return semantics.

Comparing .text byte-for-byte at fixed VMAs is misleading when equivalent
eight-byte erratum stubs are placed a few bytes apart. This checks the five
real GNU Linux 6.6.143 AArch64 erratum patches against MiniLD without using
its internal profiler or assuming MiniLD exports BFD e843419@ labels.
"""

from pathlib import Path
import re
import struct
import sys


def fail(message):
    raise SystemExit("MINILD_AARCH64_A53_ORACLE=FAIL " + message)


def load_text(path):
    data = Path(path).read_bytes()
    if data[:6] != b"\x7fELF\x02\x01":
        fail("input_not_ELF64LE=" + str(path))
    shoff = struct.unpack_from("<Q", data, 40)[0]
    shnum, shstrndx = struct.unpack_from("<HH", data, 60)
    if shoff + shnum * 64 > len(data) or shstrndx >= shnum:
        fail("invalid_elf_section_headers=" + str(path))
    shstr = struct.unpack_from("<IIQQQQIIQQ", data, shoff + shstrndx * 64)
    names = data[shstr[4]:shstr[4] + shstr[5]]
    for i in range(shnum):
        entry = struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * 64)
        nameend = names.find(b"\0", entry[0])
        if nameend < 0:
            fail("unterminated_section_name=" + str(path))
        if names[entry[0]:nameend] != b".text":
            continue
        address, start, size = entry[3:6]
        if start + size > len(data):
            fail("truncated_text=" + str(path))
        return address, data[start:start + size]
    fail("missing_text=" + str(path))


def branch_target(insn, address):
    if insn is None or insn & 0xFC000000 != 0x14000000:
        return None
    imm = insn & 0x03FFFFFF
    if imm & 0x02000000:
        imm -= 0x04000000
    return address + imm * 4


def word(address, start, text):
    off = address - start
    if off < 0 or off + 4 > len(text) or off % 4:
        return None
    return struct.unpack_from("<I", text, off)[0]


def main():
    if len(sys.argv) != 4:
        fail("usage=GNU-first ELF MiniLD-first ELF GNU-System.map")
    gnu_base, gnu = load_text(sys.argv[1])
    mini_base, mini = load_text(sys.argv[2])
    if gnu_base != mini_base or len(gnu) != len(mini):
        fail("output_text_layout_mismatch")
    stubs = []
    for line in Path(sys.argv[3]).read_text().splitlines():
        match = re.fullmatch(r"([a-fA-F0-9]{16})\s+[tT]\s+(e843419@\S+)", line)
        if match:
            stubs.append((int(match[1], 16), match[2]))
    if len(stubs) != 5:
        fail("expected_five_GNU_A53_stubs_found=" + str(len(stubs)))
    owners = ("kernel/time/clocksource.o", "lib/group_cpus.o",
              "drivers/xen/events/events_base.o",
              "drivers/firmware/efi/vars.o", "net/ipv4/igmp.o")

    for i, (gnu_island, name) in enumerate(sorted(stubs)):
        sources = [va for va in range(max(gnu_base, gnu_island - 0x10000),
                                      gnu_island, 4)
                   if branch_target(word(va, gnu_base, gnu), va) == gnu_island]
        if len(sources) != 1:
            fail(f"GNU_patch_{i + 1}_branch_count={len(sources)}")
        gnu_source = sources[0]
        copied = word(gnu_island, gnu_base, gnu)
        if branch_target(word(gnu_island + 4, gnu_base, gnu),
                         gnu_island + 4) != gnu_source + 4:
            fail(f"GNU_patch_{i + 1}_return_invalid")
        matches = []
        for candidate in range(gnu_source - 64, gnu_source + 65, 4):
            destination = branch_target(word(candidate, mini_base, mini), candidate)
            if destination is None or not candidate < destination <= candidate + 0x10000:
                continue
            if word(destination, mini_base, mini) != copied:
                continue
            if branch_target(word(destination + 4, mini_base, mini),
                             destination + 4) != candidate + 4:
                continue
            matches.append((candidate, destination))
        if len(matches) != 1:
            fail(f"Mini_patch_{i + 1}_semantic_match_count={len(matches)}")
        mini_source, mini_island = matches[0]
        print(f"MINILD_AARCH64_A53_PATCH=PASS index={i + 1} "
              f"owner={owners[i]} GNU_source={gnu_source:#x} "
              f"Mini_source={mini_source:#x} GNU_stub={gnu_island:#x} "
              f"Mini_stub={mini_island:#x} insn={copied:#010x} "
              f"source_delta={mini_source - gnu_source} "
              f"island_delta={mini_island - gnu_island} GNU_symbol={name}")
    print("MINILD_AARCH64_A53_ORACLE=PASS patches=5 "
          "GNU_and_MiniLD_both_branch_copy_and_return=true Linux_P1=NOT_RUN")


if __name__ == "__main__":
    main()
