#!/usr/bin/env python3
"""Inspect BFD AArch64 kernel RELA ordinals without trusting a dead .dynsym.

The real Linux linker script discards .dynsym, but BFD has already numbered
symbols used by .rela.dyn.  A GNU-only shadow link retaining .dynsym provides
the independent names behind those numeric r_sym fields.  This audit consumes
those two ELFs and optionally the MiniLD result; it never guesses symbol names
from the final .symtab, which has unrelated indices.
"""

from __future__ import annotations

import argparse
import pathlib
import struct


def elf_sections(path: pathlib.Path):
    blob = path.read_bytes()
    if blob[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", blob, 18)[0] != 183:
        raise ValueError(f"expected little-endian ELF64 AArch64: {path}")
    shoff = struct.unpack_from("<Q", blob, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", blob, 58)
    if shentsize != 64 or not 0 < shstrndx < shnum:
        raise ValueError(f"invalid ELF section header table: {path}")
    sections = [struct.unpack_from("<IIQQQQIIQQ", blob, shoff + i * shentsize)
                for i in range(shnum)]
    names_section = sections[shstrndx]
    names = blob[names_section[4]:names_section[4] + names_section[5]]
    result = {}
    for section in sections:
        start = section[0]
        end = names.find(b"\0", start)
        if end < 0:
            raise ValueError(f"invalid section name offset: {path}")
        result[names[start:end].decode()] = section
    return blob, sections, result


def rela(path: pathlib.Path):
    blob, _sections, named = elf_sections(path)
    section = named[".rela.dyn"]
    if section[1] != 4 or section[9] != 24 or section[5] % 24:
        raise ValueError(f"invalid AArch64 RELA section: {path}")
    result = []
    for off in range(section[4], section[4] + section[5], 24):
        addr, info, addend = struct.unpack_from("<QQq", blob, off)
        result.append((addr, info >> 32, info & 0xffffffff, addend))
    return result


def dynsym(path: pathlib.Path):
    blob, sections, named = elf_sections(path)
    section_names = [next((name for name, header in named.items()
                           if header is section), "") for section in sections]
    section = named[".dynsym"]
    if section[1] != 11 or section[9] != 24 or section[5] % 24:
        raise ValueError(f"invalid dynamic symbol table: {path}")
    string_section = sections[section[6]]
    names = blob[string_section[4]:string_section[4] + string_section[5]]
    result = []
    for off in range(section[4], section[4] + section[5], 24):
        name_offset, info, other, shndx, value, size = struct.unpack_from(
            "<IBBHQQ", blob, off)
        end = names.find(b"\0", name_offset)
        if end < 0:
            raise ValueError(f"invalid dynamic symbol name offset: {path}")
        name = names[name_offset:end].decode()
        if not name and (info & 0xf) == 3 and shndx < len(section_names):
            name = section_names[shndx]
        result.append((name, info >> 4,
                       info & 0xf, other, shndx, value, size))
    return result


def bfd_link_hash(name: str):
    """BFD's symbol-name hash (bfd/hash.c), including its length mix."""
    mask = (1 << 64) - 1
    value = 0
    for byte in name.encode():
        value = (value + byte + (byte << 17)) & mask
        value ^= value >> 2
    length = len(name.encode())
    value = (value + length + (length << 17)) & mask
    value ^= value >> 2
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gnu_baseline", type=pathlib.Path)
    parser.add_argument("gnu_shadow", type=pathlib.Path)
    parser.add_argument("--mini", type=pathlib.Path)
    parser.add_argument("--require-mini-parity", action="store_true",
                        help="fail until MiniLD symbolic r_sym/type matches GNU exactly")
    parser.add_argument("--bfd-buckets", type=int,
                        help="BFD's link-hash table bucket count, if proven from the input")
    args = parser.parse_args()
    if args.require_mini_parity and args.mini is None:
        parser.error("--require-mini-parity requires --mini")
    baseline = [record for record in rela(args.gnu_baseline) if record[1]]
    shadow = [record for record in rela(args.gnu_shadow) if record[1]]
    symbols = dynsym(args.gnu_shadow)
    if len(baseline) != len(shadow) or [v[1:3] for v in baseline] != [
            v[1:3] for v in shadow]:
        raise ValueError("shadow script changed GNU symbolic relocation ordinals")
    section_locals = []
    forced_locals = []
    for index, symbol in enumerate(symbols[1:], 1):
        name, binding, kind, _other, _section, _value, _size = symbol
        if binding != 0:
            break
        if kind == 3:
            section_locals.append((index, name))
        else:
            forced_locals.append((index, name))
    globals_ = [(index, symbols[index][0]) for index in range(1, len(symbols))
                if symbols[index][1] != 0]
    print(f"AARCH64_DYNSYM_MODEL sections={len(section_locals)} "
          f"forced_local={len(forced_locals)} globals={len(globals_)} "
          f"total={len(symbols)}")
    print("section_locals=" + ",".join(f"{i}:{n}" for i, n in section_locals))
    print("forced_locals=" + ",".join(f"{i}:{n}" for i, n in forced_locals))
    for address, index, kind, _addend in baseline:
        if not 0 < index < len(symbols):
            raise ValueError(f"invalid GNU dynamic ordinal {index}")
        name, binding, _type, _other, shndx, _value, _size = symbols[index]
        if binding == 0 or shndx != 0:
            raise ValueError(f"symbol-bearing GNU RELA points to unexpected dynsym {index}")
        print(f"GNU_SYMBOLIC_RELA place=0x{address:x} r_sym={index} "
              f"type={kind} name={name}")
    if args.bfd_buckets is not None:
        if args.bfd_buckets <= 0:
            raise ValueError("BFD hash bucket count must be positive")
        unique = sorted(set(index for _addr, index, _kind, _addend in baseline))
        observed = [symbols[index][0] for index in unique]
        expected = sorted(observed, key=lambda name: bfd_link_hash(name) % args.bfd_buckets)
        if observed != expected:
            raise ValueError("symbolic global ordinals are not BFD hash-bucket ordered")
        print(f"AARCH64_BFD_HASH_ORDER=PASS buckets={args.bfd_buckets} "
              f"symbols={len(observed)}")
    if args.mini is not None:
        mini = [record for record in rela(args.mini) if record[1]]
        identity = [(sym, typ) for _place, sym, typ, _a in baseline]
        observed = [(sym, typ) for _place, sym, typ, _a in mini]
        status = "PASS" if identity == observed else "DIFF"
        print(f"AARCH64_MINILD_SYMBOLIC_ORDINAL={status} "
              f"gnu={len(identity)} mini={len(observed)}")
        if args.require_mini_parity and identity != observed:
            raise ValueError("MiniLD symbolic r_sym/type differs from GNU")
    print("AARCH64_GNU_SHADOW_ORDINAL=PASS")


if __name__ == "__main__":
    main()
