#!/usr/bin/env python3
"""Prove that the ELF comparator decodes, rather than discards, i386 REL A.

GNU and MiniAS have identical narrow relocation semantics, but two ELF32
objects with the same relocation symbol/type and different in-place addends
must compare unequal even though their relocated byte fields are masked.
"""

import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from elftools.elf.elffile import ELFFile

from elf_semantic_compare import compare_paths, load_semantics


ROOT = Path(__file__).resolve().parents[2]
MINIAS = os.environ.get("MINIAS", str(ROOT / "build/release/bin/minic-as"))
FIXTURE = ROOT / "tests/assembler/fixtures/x86-narrow-symbol-relocs.S"


def check(command):
    subprocess.run(command, check=True, stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE, text=True)


def layout(path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        assert str(elf.header["e_machine"]) == "EM_386"
        text = elf.get_section_by_name(".text")
        reloc = elf.get_section_by_name(".rel.text")
        assert text is not None and reloc is not None and not reloc.is_RELA()
        assert reloc.num_relocations() == 5
        return (int(text["sh_offset"]), int(reloc["sh_offset"]),
                int(text["sh_size"]))


def altered_copy(source, destination, at, data):
    bits = bytearray(source.read_bytes())
    bits[at:at + len(data)] = data
    destination.write_bytes(bits)


def semantic_reloc_without_addends(loaded):
    return [(row["key"][:4], row["count"])
            for row in loaded["relocations"]]


def main():
    if shutil.which("as") is None:
        raise SystemExit("MINIAS_I386_REL_ADDENDS=BLOCKED missing GNU as")
    if not Path(MINIAS).is_file():
        raise SystemExit("MINIAS_I386_REL_ADDENDS=BLOCKED missing MiniAS")

    with tempfile.TemporaryDirectory(prefix="minias-i386-rel-") as temp:
        work = Path(temp)
        gnu, mini = work / "gnu.o", work / "mini.o"
        check(["as", "--32", "-o", str(gnu), str(FIXTURE)])
        check([MINIAS, "--target=x86_64", "-march=i386", "-mabi=i386",
               "-o", str(mini), str(FIXTURE)])
        matched = compare_paths(gnu, mini)
        assert matched["strict_equal"] and not matched["inconclusive"], matched
        original = load_semantics(gnu)
        assert len(original["relocations"]) == 5, original["relocations"]
        assert {(row["key"][2], row["key"][4]) for row in original["relocations"]} == {
            (22, 1), (20, 2), (23, 0), (21, 0), (1, 3)
        }, original["relocations"]

        text_offset, rel_offset, text_size = layout(gnu)
        assert text_size == 10
        changed_addend = work / "changed-addend.o"
        # The first relocation remains R_386_8 against abs_byte. Only its
        # field changes from 1 to 2: the eventual linked byte changes too.
        altered_copy(gnu, changed_addend, text_offset, b"\x02")
        after = load_semantics(changed_addend)
        assert semantic_reloc_without_addends(after) == \
            semantic_reloc_without_addends(original)
        result = compare_paths(gnu, changed_addend)
        assert not result["strict_equal"] and not result["inconclusive"], result
        assert not result["dimensions"]["relocations"], result
        assert result["dimensions"]["contents_masked"], result

        # A relocation with unmodeled R_386_GOTPC semantics cannot have
        # its original bytes removed from the hash as if it were R_386_8.
        unknown = work / "unmodeled-i386-rel.o"
        raw = bytearray(gnu.read_bytes())
        info = struct.unpack_from("<I", raw, rel_offset + 4)[0]
        assert info & 0xff == 22  # R_386_8
        struct.pack_into("<I", raw, rel_offset + 4, (info & ~0xff) | 10)
        unknown.write_bytes(raw)
        result = compare_paths(unknown, unknown)
        assert not result["strict_equal"] and result["inconclusive"], result
        assert result["reference_skipped_content"], result

        # A recognized width extending beyond the section is malformed;
        # fail before hashing or comparing any masked bytes.
        malformed = work / "out-of-range-i386-rel.o"
        altered_copy(gnu, malformed, rel_offset + 8, struct.pack("<I", 9))
        try:
            load_semantics(malformed)
        except ValueError as error:
            assert "outside target section" in str(error), error
        else:
            raise AssertionError("out-of-range i386 REL field was accepted")

    print("MINIAS_I386_REL_ADDENDS=PASS GNU_MiniAS_strict=1 "
          "REL_addend_mutation=detected unknown_form=inconclusive "
          "field_bounds=checked")


if __name__ == "__main__":
    main()
