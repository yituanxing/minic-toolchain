#!/usr/bin/env python3
"""No ELF table name may hide a real colliding PROGBITS payload.

Create two valid GNU x86 objects, then corrupt only a user section's sh_name
to collide with a generated ELF table. This adversarial ET_REL fixture is
deliberately malformed as an assembler *source* construct but still readable
as ELF; an equivalence checker must fail closed rather than discard the bytes.
"""

import pathlib
import struct
import subprocess
import sys
import tempfile

from elftools.elf.elffile import ELFFile

from elf_semantic_compare import compare_paths, load_semantics


def rename_user_section(source, output, replacement):
    with source.open("rb") as file:
        elf = ELFFile(file)
        if elf.elfclass != 64 or not elf.little_endian:
            raise AssertionError("GNU x86-64 test object must be little-endian ELF64")
        shstrtab = elf.get_section(int(elf.header["e_shstrndx"]))
        replacement_offset = shstrtab.data().find(replacement.encode() + b"\0")
        if replacement_offset < 0:
            raise AssertionError(f"GNU object lacks generated {replacement} name")
        names = [sec.name for sec in elf.iter_sections()]
        index = names.index(".payload_table_name")
        header_offset = (int(elf.header["e_shoff"]) +
                         index * int(elf.header["e_shentsize"]))
    data = bytearray(source.read_bytes())
    struct.pack_into("<I", data, header_offset, replacement_offset)
    output.write_bytes(data)


def main():
    with tempfile.TemporaryDirectory(prefix="minias-elf-reserved-") as temp:
        root = pathlib.Path(temp)
        for label, value in (("first", 0x11), ("second", 0x22)):
            source = f'.section .payload_table_name,"a",@progbits\n.byte {value}\n'
            subprocess.run(["as", "--64", "-o", str(root / f"{label}.o"), "-"],
                           input=source, text=True, check=True)
        for collision in (".symtab", ".strtab", ".shstrtab"):
            label = collision[1:]
            files = []
            for name in ("first", "second"):
                renamed = root / f"{name}-{label}.o"
                rename_user_section(root / f"{name}.o", renamed, collision)
                files.append(renamed)
            first = load_semantics(files[0])
            second = load_semantics(files[1])
            # The structural table is still excluded by (name,type); the
            # colliding PROGBITS payload is not. Canonical .symtab must not
            # be shadowed by the earlier spoof section.
            if first["contents_masked"] == second["contents_masked"]:
                raise AssertionError(f"{collision}: payload difference was hidden")
            verdict = compare_paths(*files)
            if verdict["equal"] or verdict["dimensions"]["contents_masked"]:
                raise AssertionError(f"{collision}: comparator false-green: {verdict}")
        print("MINIAS_ELF_RESERVED_TABLE_NAMES=PASS collisions=3 "
              "PROGBITS_payload_mutations=detected symbol_table=typed")


if __name__ == "__main__":
    sys.exit(main())
