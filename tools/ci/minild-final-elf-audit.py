#!/usr/bin/env python3
"""Fail-closed ELF32/ELF64 final-link comparison for frozen GNU Linux inputs.

This is a structural gate for the first kallsyms1 link, not a guest boot
certification. No external Python modules or target executable are required.
"""
from collections import Counter
from pathlib import Path
import struct
import sys


class InvalidELF(ValueError):
    pass


def check(condition, message):
    if not condition:
        raise InvalidELF(message)


def within(start, length, total):
    return start >= 0 and length >= 0 and start <= total and length <= total - start


def read_elf(path):
    data = Path(path).read_bytes()
    check(len(data) >= 16 and data[:4] == b"\x7fELF", "missing ELF magic")
    elf_class, encoding = data[4], data[5]
    check(elf_class in (1, 2) and encoding in (1, 2),
          "unsupported ELF class or endianness")
    endian = "<" if encoding == 1 else ">"
    is64 = elf_class == 2
    header_fmt = endian + ("HHIQQQIHHHHHH" if is64 else "HHIIIIIHHHHHH")
    header_size = 64 if is64 else 52
    check(len(data) >= header_size, "truncated ELF header")
    (etype, machine, version, entry, phoff, shoff, flags, ehsize,
     phentsize, phnum, shentsize, shnum, shstrndx) = struct.unpack_from(
         header_fmt, data, 16)
    check(version == 1 and ehsize == header_size, "invalid ELF header size/version")
    check(etype == 2, "kernel first-link output must be ET_EXEC")
    check(0 < phnum < 65535 and 0 < shnum < 65535,
          "missing or extended ELF tables are not admitted")
    check(phentsize == (56 if is64 else 32) and
          shentsize == (64 if is64 else 40), "invalid ELF table entry size")
    check(within(phoff, phnum * phentsize, len(data)) and
          within(shoff, shnum * shentsize, len(data)),
          "ELF table extends beyond the file")

    loads = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        if is64:
            kind, perms, fileoff, vaddr, paddr, filesz, memsz, align = (
                struct.unpack_from(endian + "IIQQQQQQ", data, offset))
        else:
            kind, fileoff, vaddr, paddr, filesz, memsz, perms, align = (
                struct.unpack_from(endian + "IIIIIIII", data, offset))
        if kind != 1:  # PT_LOAD
            continue
        check(memsz >= filesz, "PT_LOAD p_memsz smaller than p_filesz")
        check(within(fileoff, filesz, len(data)),
              "PT_LOAD exceeds backing file")
        check(align in (0, 1) or
              (align & (align - 1) == 0 and
               (vaddr - fileoff) % align == 0),
              "PT_LOAD file/VMA congruence violated")
        check(vaddr + memsz <= (1 << (64 if is64 else 32)),
              "PT_LOAD virtual address wraps")
        loads.append((perms, fileoff, vaddr, paddr, filesz, memsz, align))
    check(loads, "no PT_LOAD mappings")
    if entry:
        check(any((perms & 1) and vaddr <= entry < vaddr + memsz
                  for perms, _, vaddr, _, _, memsz, _ in loads),
              "entry point is outside executable PT_LOAD")

    shfmt = endian + ("IIQQQQIIQQ" if is64 else "IIIIIIIIII")
    sections = []
    for index in range(shnum):
        fields = struct.unpack_from(shfmt, data, shoff + index * shentsize)
        name, kind, flags_sh, address, fileoff, size = fields[:6]
        sections.append((name, kind, flags_sh, address, fileoff, size))
    check(0 < shstrndx < shnum, "missing section-name table")
    _, strtype, _, _, stroff, strlen = sections[shstrndx]
    check(strtype == 3 and within(stroff, strlen, len(data)),
          "invalid section-name table")
    names = data[stroff:stroff + strlen]
    alloc = []
    for nameoff, kind, flags_sh, address, fileoff, size in sections:
        check(nameoff < len(names), "section name offset out of bounds")
        nameend = names.find(b"\x00", nameoff)
        check(nameend >= 0, "unterminated section name")
        name = names[nameoff:nameend].decode("utf-8", "replace")
        if kind != 8:  # SHT_NOBITS: its file offset may legally be beyond EOF.
            check(within(fileoff, size, len(data)),
                  "file-backed section extends beyond EOF: " + name)
        if flags_sh & 2 and size:  # SHF_ALLOC, not an empty placeholder.
            check(address + size <= (1 << (64 if is64 else 32)),
                  "allocated section address wraps: " + name)
            mapped = False
            for _, load_fileoff, load_vaddr, _, filesz, memsz, _ in loads:
                if not (load_vaddr <= address and
                        size <= load_vaddr + memsz - address):
                    continue
                if kind == 8:
                    mapped = True
                    break
                if (load_fileoff <= fileoff and
                        fileoff - load_fileoff == address - load_vaddr and
                        size <= load_fileoff + filesz - fileoff):
                    mapped = True
                    break
            check(mapped, "allocated section is not correctly PT_LOAD-mapped: " +
                  name)
            alloc.append((name, kind, flags_sh, address, size))
    return ((elf_class, encoding, etype, machine, flags, entry),
            Counter(alloc), len(loads))


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: minild-final-elf-audit.py GNU.elf MiniLD.elf")
    try:
        gnu_id, gnu_sections, gnu_loads = read_elf(sys.argv[1])
        mini_id, mini_sections, mini_loads = read_elf(sys.argv[2])
        check(gnu_id == mini_id,
              "GNU/MiniLD ELF identity, ABI flags or entrypoint differs: " +
              repr((gnu_id, mini_id)))
        missing = gnu_sections - mini_sections
        extra = mini_sections - gnu_sections
        check(not missing and not extra,
              "allocated section layout differs: missing=" +
              repr(list(missing.elements())[:8]) + " extra=" +
              repr(list(extra.elements())[:8]))
        print("MINILD_FINAL_ELF_AUDIT=PASS identity=exact "
              "entrypoint=exact allocated_sections={} "
              "gnu_loads={} mini_loads={} "
              "load_mapping=validated".format(
                  sum(gnu_sections.values()), gnu_loads, mini_loads))
    except (InvalidELF, OSError, struct.error) as exc:
        print("MINILD_FINAL_ELF_AUDIT=FAIL " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
