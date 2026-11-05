#!/usr/bin/env python3
"""Fail-closed GNU ELF oracle mutation tests (no target compiler required).

Construct minimal valid ELF64 relocatable objects whose sections and RELA
entries have the documented psABI shapes, then mutate exactly one physical
field. The two objects retain identical symbol/relocation records, forcing
the semantic oracle to catch differences outside the linker-written bits.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import tempfile


ROOT = Path(__file__).resolve().parent
MODULE = ROOT / "elf_semantic_compare.py"
spec = importlib.util.spec_from_file_location("minias_semantic_oracle", MODULE)
assert spec is not None and spec.loader is not None
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

ELF_HEADER = "<16sHHIQQQIHHHHHH"
SECTION_HEADER = "<IIQQQQIIQQ"
SYMBOL = "<IBBHQQ"
RELA = "<QQq"


def elf_object(*, machine: int, relocation: int, instruction: bytes,
               reloc_offset: int = 0, alignment: int = 4) -> tuple[bytes, dict]:
    """Return ELF bytes plus byte offsets for every mutation target."""
    note = (struct.pack("<III", 4, 4, 1) + b"GNU\0" + b"ABCD")
    symtab = (bytes(24) + struct.pack(SYMBOL, 1, 0x10, 0, 0, 0, 0))
    names = ["", ".text", ".rela.text", ".symtab", ".strtab",
             ".shstrtab", ".note.demo", ".init_array", ".fini_array",
             ".preinit_array", ".riscv.attributes"]
    shnames = bytearray(b"\0")
    name_offsets = {}
    for name in names[1:]:
        name_offsets[name] = len(shnames)
        shnames.extend(name.encode() + b"\0")
    records = [
        # name, type, flags, alignment, entsize, link, info, payload
        (".text", 1, 0x6, alignment, 0, 0, 0, instruction),
        (".rela.text", 4, 0, 8, 24, 3, 1,
         struct.pack(RELA, reloc_offset, (1 << 32) | relocation, 0)),
        (".symtab", 2, 0, 8, 24, 4, 1, symtab),
        (".strtab", 3, 0, 1, 0, 0, 0, b"\0target\0"),
        (".shstrtab", 3, 0, 1, 0, 0, 0, bytes(shnames)),
        (".note.demo", 7, 0, 4, 0, 0, 0, note),
        (".init_array", 14, 0x3, 8, 8, 0, 0, b"INITDATA"),
        (".fini_array", 15, 0x3, 8, 8, 0, 0, b"FINIDATA"),
        (".preinit_array", 16, 0x3, 8, 8, 0, 0, b"PREINIT!"),
        (".riscv.attributes", 0x70000003, 0, 1, 0, 0, 0,
         b"A\x10\x00\x00\x00arch_data!"),
    ]
    image = bytearray(bytes(64))
    headers = [bytes(64)]
    locations = {}
    section_headers = {}
    for i, (name, typ, flags, align, entsize, link, info, data) in enumerate(
            records, 1):
        image.extend(bytes((-len(image)) % align))
        start = len(image)
        image.extend(data)
        locations[name] = start
        section_headers[name] = i
        headers.append(struct.pack(SECTION_HEADER, name_offsets[name], typ,
                                   flags, 0, start, len(data), link, info,
                                   align, entsize))
    image.extend(bytes((-len(image)) % 8))
    shoff = len(image)
    for header in headers:
        image.extend(header)
    ident = b"\x7fELF\x02\x01\x01" + bytes(9)
    image[:64] = struct.pack(ELF_HEADER, ident, 1, machine, 1, 0, 0, shoff,
                             0, 64, 0, 0, 64, len(headers), 5)
    locations[".shoff"] = shoff
    locations[".rela-header"] = shoff + section_headers[".rela.text"] * 64
    return bytes(image), locations


def changed(image: bytes, offset: int, value: bytes) -> bytes:
    result = bytearray(image)
    result[offset:offset + len(value)] = value
    return bytes(result)


def check(reference: bytes, candidate: bytes, expected_equal: bool,
          *, strict: bool | None = None, inconclusive: bool = False):
    with tempfile.TemporaryDirectory(prefix="minias-elf-oracle-") as temp:
        base = Path(temp)
        a, b = base / "reference.o", base / "candidate.o"
        a.write_bytes(reference)
        b.write_bytes(candidate)
        actual = oracle.compare_paths(a, b)
        assert actual["equal"] is expected_equal, actual
        assert actual["inconclusive"] is inconclusive, actual
        if strict is not None:
            assert actual["strict_equal"] is strict, actual
        return actual


def main():
    variants = {
        "rv-branch": (243, 16, 0x00000063, 0x00001063, 0x80000063),
        "arm64-adrp": (183, 275, 0x90000000, 0x90000001, 0xb0000000),
        "loong-b16": (258, 64, 0x40000000, 0x40000020, 0x40000400),
    }
    for name, (machine, reloc, opcode, bad_opcode, good_immediate) in variants.items():
        source, positions = elf_object(machine=machine, relocation=reloc,
                                       instruction=struct.pack("<I", opcode))
        target = positions[".text"]
        wrong_instruction = changed(source, target, struct.pack("<I", bad_opcode))
        changed_immediate = changed(source, target, struct.pack("<I", good_immediate))
        assert not check(source, wrong_instruction, False)["dimensions"]["contents_masked"]
        check(source, changed_immediate, True, strict=True)
        print(f"MINIAS_ORACLE_BITFIELD=PASS target={name} opcode_negative=1 "
              "relocated_immediate_positive=1")

    # x86 relocation covers only the *displacement*, not the preceding
    # CALL/JMP opcode. A whole 4-byte PC32 mask remains safe in this case.
    x86, pos = elf_object(machine=62, relocation=2,
                          instruction=b"\xe8\0\0\0\0", reloc_offset=1)
    check(x86, changed(x86, pos[".text"], b"\xe9"), False)
    check(x86, changed(x86, pos[".text"] + 1, b"\x7f\0\0\0"), True)
    print("MINIAS_ORACLE_X86_DISPLACEMENT=PASS opcode_negative=1 immediate_positive=1")

    for name, start in pos.items():
        if name not in (".note.demo", ".init_array", ".fini_array",
                        ".preinit_array", ".riscv.attributes"):
            continue
        other = changed(x86, start + (12 if name == ".note.demo" else 0), b"Z")
        result = check(x86, other, False)
        assert result["dimensions"]["section_core"], result
        assert not result["dimensions"]["contents_masked"], result
        print(f"MINIAS_ORACLE_SECTION_CONTENT=PASS type={name} mutated_byte=1")

    # A REL record has no explicit r_addend. Refuse to fabricate a zero
    # addend even when comparing two byte-identical SHT_REL files.
    source, where = elf_object(machine=243, relocation=16,
                               instruction=struct.pack("<I", 0x00000063))
    rel = changed(source, where[".rela-header"] + 4, struct.pack("<I", 9))
    rel = changed(rel, where[".rela-header"] + 32, struct.pack("<Q", 16))
    rel = changed(rel, where[".rela-header"] + 56, struct.pack("<Q", 16))
    result = check(rel, rel, False, inconclusive=True)
    assert not result["dimensions"]["known_relocation_widths"], result
    print("MINIAS_ORACLE_REL=PASS implicit_addend=inconclusive")

    unknown, _ = elf_object(machine=243, relocation=499,
                            instruction=struct.pack("<I", 0x00000063))
    check(unknown, unknown, False, inconclusive=True)
    print("MINIAS_ORACLE_UNKNOWN_RELOC=PASS status=inconclusive")

    # R_RISCV_ALIGN permits discarding NOP padding, but it must never mask
    # arbitrary executable bytes. Its addend denotes the padding extent.
    align, loc = elf_object(machine=243, relocation=43,
                            instruction=struct.pack("<I", 0x00000013))
    align = changed(align, loc[".rela.text"] + 16, struct.pack("<q", 4))
    check(align, align, True, strict=True)
    wrong_nop = changed(align, loc[".text"], struct.pack("<I", 0x00100013))
    try:
        check(align, wrong_nop, False)
    except ValueError as exc:
        assert "R_RISCV_ALIGN covers non-NOP" in str(exc), exc
    else:
        raise AssertionError("oracle masked R_RISCV_ALIGN non-NOP bytes")
    print("MINIAS_ORACLE_ALIGN_NOP=PASS canonical_positive=1 forged_negative=1")

    # ELF SHNDX side tables have symtab-order-specific index payloads. Their
    # raw equality alone is not enough to certify symbolic equivalence.
    side_table = changed(x86, pos[".shoff"] + 10 * 64 + 4,
                         struct.pack("<I", 18))
    check(side_table, side_table, False, inconclusive=True)
    print("MINIAS_ORACLE_SYMTAB_SHNDX=PASS unsupported=inconclusive")

    aligned, a_pos = elf_object(machine=62, relocation=2,
                                instruction=b"\xe8\0\0\0\0", reloc_offset=1)
    shoff = a_pos[".shoff"]
    # .text is section #1, sh_addralign at +48 in its ELF64 Shdr.
    wider = changed(aligned, shoff + 64 + 48, struct.pack("<Q", 8))
    compatible = check(aligned, wider, True, strict=False)
    assert compatible["alignment_verdict"] == "compatible", compatible
    incompatible = check(wider, aligned, False, strict=False)
    assert incompatible["alignment_verdict"] == "incompatible", incompatible
    print("MINIAS_ORACLE_ALIGNMENT=PASS compatible_not_strict=1 incompatible=1")
    print("MINIAS_ORACLE_PAYLOAD_BITFIELDS=PASS")


if __name__ == "__main__":
    main()
