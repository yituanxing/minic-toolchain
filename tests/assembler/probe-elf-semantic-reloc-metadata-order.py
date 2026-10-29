#!/usr/bin/env python3
"""Mutation-proof the GNU ELF comparator's non-bytecode object semantics.

These minimal ELF64 ET_REL fixtures are independent of cross-assemblers.
Every mutation leaves the other ELF structures untouched, so an oracle that
compares only relocation multisets, target bytes and section flags will
falsely pass multiple negative cases below.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


ORACLE = Path(__file__).with_name("elf_semantic_compare.py")
spec = importlib.util.spec_from_file_location("minias_semantic_oracle_metadata", ORACLE)
assert spec is not None and spec.loader is not None
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

ELF_HEADER = "<16sHHIQQQIHHHHHH"
SECTION_HEADER = "<IIQQQQIIQQ"
SYMBOL = "<IBBHQQ"
RELA = "<QQq"


def make_elf(machine: int, *, text: bytes = b"\0" * 8,
             relocations: tuple[tuple[int, int, int], ...] = ((0, 1, 0),),
             target_flags: int = 0x6) -> tuple[bytes, dict[str, int]]:
    """Build one symtab and relocation table with inspectable raw offsets."""
    names = ("", ".text", ".rela.text", ".symtab", ".strtab",
             ".shstrtab", ".text.extra", ".dependent", ".note.demo")
    shstrings = bytearray(b"\0")
    name_offset = {}
    for name in names[1:]:
        name_offset[name] = len(shstrings)
        shstrings.extend(name.encode() + b"\0")

    symtab = bytes(24) + struct.pack(SYMBOL, 1, 0x10, 0, 0, 0, 0)
    rela = b"".join(struct.pack(RELA, offset, (1 << 32) | typ, addend)
                    for offset, typ, addend in relocations)
    records = [
        # name, type, flags, alignment, entsize, sh_link, sh_info, payload
        (".text", 1, target_flags, 4, 0, 0, 0, text),
        (".rela.text", 4, 0x40, 8, 24, 3, 1, rela),
        (".symtab", 2, 0, 8, 24, 4, 1, symtab),
        (".strtab", 3, 0, 1, 0, 0, 0, b"\0target\0"),
        (".shstrtab", 3, 0, 1, 0, 0, 0, bytes(shstrings)),
        (".text.extra", 1, 0x6, 4, 0, 0, 0, b"\x90\x90\x90\x90"),
        (".dependent", 1, 0x40, 4, 0, 0, 1, b"DATA"),
        (".note.demo", 7, 0, 4, 0, 0, 0,
         struct.pack("<III", 4, 4, 1) + b"GNU\0" + b"ABCD"),
    ]
    image = bytearray(64)
    sections = [bytes(64)]
    where = {}
    for number, (name, typ, flags, align, entsize, link, info, payload) in enumerate(
            records, 1):
        image.extend(bytes((-len(image)) % align))
        where[f"data:{name}"] = len(image)
        where[f"section-index:{name}"] = number
        image.extend(payload)
        sections.append(struct.pack(SECTION_HEADER, name_offset[name], typ,
                                    flags, 0, where[f"data:{name}"], len(payload),
                                    link, info, align, entsize))
    image.extend(bytes((-len(image)) % 8))
    where["shoff"] = len(image)
    for number, header in enumerate(sections):
        where[f"shdr:{number}"] = len(image)
        image.extend(header)
    ident = b"\x7fELF\x02\x01\x01" + bytes(9)
    image[:64] = struct.pack(ELF_HEADER, ident, 1, machine, 1, 0, 0,
                             where["shoff"], 0, 64, 0, 0, 64,
                             len(sections), 5)
    return bytes(image), where


def mutate(data: bytes, offset: int, update: bytes) -> bytes:
    result = bytearray(data)
    result[offset:offset + len(update)] = update
    return bytes(result)


def compare(reference: bytes, candidate: bytes, *, equal: bool,
            inconclusive: bool = False, failed: str | None = None):
    with tempfile.TemporaryDirectory(prefix="minias-oracle-order-") as temp:
        base = Path(temp)
        first, second = base / "gnu.o", base / "minias.o"
        first.write_bytes(reference)
        second.write_bytes(candidate)
        result = oracle.compare_paths(first, second)
    assert result["equal"] == equal, result
    assert result["inconclusive"] == inconclusive, result
    if failed:
        assert not result["dimensions"][failed], result
    return result


def case(label: str, reference: bytes, candidate: bytes, dimension: str):
    compare(reference, candidate, equal=False, failed=dimension)
    print(f"MINIAS_ORACLE_MUTATION=PASS case={label} dimension={dimension}")


def reject(label: str, data: bytes, error: str):
    """Never certify an identically malformed pair of ELF inputs."""
    try:
        compare(data, data, equal=True)
    except ValueError as exc:
        assert error in str(exc), (label, exc)
    else:
        raise AssertionError(f"oracle accepted malformed ELF: {label}")
    print(f"MINIAS_ORACLE_REJECT=PASS case={label}")


def main():
    # RISC-V ELF psABI Table 16: ADD/SUB use V +/- S +/- A, and SUB6 uses
    # the previous 6-bit value. LoongArch ABI Table 10: ADD/SUB are likewise
    # in-place. Their source bytes must not be masked like SET/ABS relocations.
    for label, machine, typ, width in (
        ("riscv-add32", 243, 35, 4),
        ("riscv-sub32", 243, 39, 4),
        ("riscv-sub6", 243, 52, 1),
        ("loong-add32", 258, 50, 4),
        ("loong-sub32", 258, 55, 4),
    ):
        original, pos = make_elf(machine, relocations=((0, typ, 0),))
        case(label, original, mutate(original, pos["data:.text"],
                                     (1).to_bytes(width, "little")),
             "contents_masked")

    # A SET32 overwrite is a legal positive case: its previous value does
    # not influence the final linked value once the RELA addend is known.
    overwritten, pos = make_elf(243, relocations=((0, 56, 0),))
    compare(overwritten, mutate(overwritten, pos["data:.text"],
                                b"\x12\x34\x56\x78"), equal=True)
    print("MINIAS_ORACLE_MUTATION=PASS case=riscv-set32-overwrite-positive")

    # R_RISCV_ALIGN's addend controls how many NOPs the linker may remove;
    # R_RISCV_RELAX only advertises optional linker optimization. A missing
    # RELAX marker is compatible but not strictly identical to GNU output.
    align, pos = make_elf(243, text=b"\x13\0\0\0" + bytes(4),
                          relocations=((0, 43, 4),))
    case("riscv-align-addend", align,
         mutate(align, pos["data:.rela.text"] + 16, struct.pack("<q", 0)),
         "relocations")
    base_relax, _ = make_elf(243, relocations=((0, 56, 0), (0, 51, 0)))
    base_without, _ = make_elf(243, relocations=((0, 56, 0),))
    result = compare(base_relax, base_without, equal=True)
    assert not result["strict_relaxation_markers"], result
    assert not result["strict_equal"] and result["dimensions"]["section_core"], result
    print("MINIAS_ORACLE_MUTATION=PASS case=riscv-relax-marker strict-only-dimension=1")

    # A relocation section's sh_flags, sh_size, sh_link and sh_info are
    # linker input. The old implementation skipped every relocation section
    # header, so adding/removing a zero-entry .rela.* went unnoticed.
    x86, pos = make_elf(62, relocations=((0, 1, 0),))
    shdr = pos["shdr:2"]
    case("rela-flags", x86, mutate(x86, shdr + 8, struct.pack("<Q", 0)),
         "section_core")
    case("rela-size", x86, mutate(x86, shdr + 32, struct.pack("<Q", 0)),
         "section_core")
    # pyelftools itself refuses malformed SHT_RELA sh_entsize; that is
    # fail-closed rather than a semantic equality verdict.
    try:
        compare(x86, mutate(x86, shdr + 56, struct.pack("<Q", 16)), equal=False)
    except Exception as exc:
        assert "Expected sh_entsize" in str(exc), exc
    else:
        raise AssertionError("malformed SHT_RELA entry stride was accepted")
    print("MINIAS_ORACLE_MUTATION=PASS case=rela-invalid-entsize verdict=error")
    # .dependent uses SHF_INFO_LINK (gABI 3.5). Both .text sections contain
    # equal object bytes, but the directed retention relationship changes.
    dep = pos["shdr:7"]
    case("shf-info-link", x86, mutate(x86, dep + 44, struct.pack("<I", 6)),
         "section_core")
    case("ordinary-sh-link", x86, mutate(x86, dep + 40, struct.pack("<I", 6)),
         "section_core")

    # st_other high 6 bits carry architecture-specific ABI semantics
    # (e.g. RISC-V STO_RISCV_VARIANT_CC at bit 7); STT_SECTION also has
    # binding, value and size, all previously silently ignored.
    sym = pos["data:.symtab"] + 24
    case("symbol-other-high-bits", x86, mutate(x86, sym + 5, b"\x80"),
         "symbols")
    section_sym = mutate(x86, sym + 4, b"\x03")
    case("stt-section-value", section_sym,
         mutate(section_sym, sym + 8, struct.pack("<Q", 4)), "symbols")
    case("stt-section-size", section_sym,
         mutate(section_sym, sym + 16, struct.pack("<Q", 4)), "symbols")

    # A64 AAELF64 §5.6.5 requires $x at the start of executable code;
    # RISC-V psABI §10.14 permits optional mapping labels but requires a
    # meaningful boundary/ISA comparison when both objects carry them.
    for name, machine in (("aarch64", 183), ("riscv", 243)):
        raw, loc = make_elf(machine, relocations=((0, 0, 0),))
        sym = loc["data:.symtab"] + 24
        mapped = mutate(raw, sym + 4, b"\0")  # LOCAL STT_NOTYPE
        mapped = mutate(mapped, loc["data:.strtab"] + 1, b"$x\0get")
        mapped = mutate(mapped, loc["data:.rela.text"] + 8,
                        struct.pack("<Q", 0))  # symbol #0, R_NONE
        data_mapping = mutate(mapped, loc["data:.strtab"] + 1, b"$d\0get")
        case(f"{name}-mapping-code-to-data", mapped, data_mapping,
             "mapping_symbols_compatible")

        # Remove the optional mapping symbol entirely; .symtab bytes are
        # normalized by the symbol model, so this is a clean compatibility
        # question rather than a raw symbol-table-size mismatch.
        no_mapping = mutate(mapped, loc["shdr:3"] + 32,
                            struct.pack("<Q", 24))
        result = compare(mapped, no_mapping, equal=(machine == 243))
        assert not result["strict_equal"], result
        assert result["dimensions"]["mapping_symbols_compatible"] is (machine == 243), result
        print(f"MINIAS_ORACLE_MUTATION=PASS case={name}-mapping-omitted "
              f"compatible={int(machine == 243)} strict=0")

    # LLVM/GNU may order relocations at *different offsets* differently;
    # same-offset order is mandatory for ADD/SUB/SET, while LoongArch SOP
    # stack-relocation ordering is mandatory across the entire table.
    for name, machine, relocations in (
        ("riscv-same-offset", 243, ((0, 56, 0), (0, 56, 1))),
        ("loong-sop-across-offsets", 258, ((0, 23, 0), (4, 46, 0))),
    ):
        source, where = make_elf(machine, relocations=relocations)
        start = where["data:.rela.text"]
        changed = mutate(source, start, source[start + 24:start + 48] +
                         source[start:start + 24])
        case(name, source, changed, "relocation_order")

    # A replacement + in-place arithmetic relocation on the same bytes is
    # deliberately INCONCLUSIVE until linker operation order is modeled.
    overlapping, _ = make_elf(243, relocations=((0, 56, 0), (0, 35, 0)))
    compare(overlapping, overlapping, equal=False, inconclusive=True,
            failed="known_relocation_widths")
    print("MINIAS_ORACLE_MUTATION=PASS case=overlapping-additive verdict=inconclusive")

    # ELF identity fields are ABI contracts, not incidental object layout.
    case("elf-osabi", x86, mutate(x86, 7, b"\x03"), "header")
    case("elf-abi-version", x86, mutate(x86, 8, b"\x01"), "header")
    case("elf-entry", x86, mutate(x86, 24, struct.pack("<Q", 1)), "header")

    # ET_REL section addresses are not necessarily an ignored header field.
    # It is incorrect to equate a nonzero-address section with a zero-based
    # one even if the payload, alignment, symbol table and relocations match.
    case("section-sh-addr", x86,
         mutate(x86, pos["shdr:1"] + 16, struct.pack("<Q", 0x4000)),
         "section_core")

    reject("et-exec-with-unchecked-load-segments",
           mutate(x86, 16, struct.pack("<H", 2)), "requires ET_REL")
    reject("unsupported-machine-without-relocations",
           mutate(x86, 18, struct.pack("<H", 3)), "no target model")
    reject("et-rel-with-program-headers",
           mutate(x86, 56, struct.pack("<H", 1)), "program headers")

    # Bounds must also be checked for nonpatching RELAX markers, ADD/SUB
    # arithmetic and unknown relocation types. Identical malformed objects
    # must never become 'equal' through matching relocation counters.
    for label, machine, typ, offset, message in (
        ("marker-offset", 243, 51, 9, "offset outside"),
        ("unknown-reloc-offset", 243, 499, 9, "offset outside"),
        ("additive-field-overflow", 243, 35, 6, "additive relocation field"),
        ("replacement-field-overflow", 62, 1, 4, "relocation field"),
    ):
        raw, _ = make_elf(machine, relocations=((offset, typ, 0),))
        reject(label, raw, message)

    # The CLI's default historical compatibility verdict is deliberately
    # distinct from exact GNU parity. An opt-in strict gate must reject
    # alignment/RELAX differences even when code remains link-compatible.
    with tempfile.TemporaryDirectory(prefix="minias-oracle-strict-cli-") as tmp:
        root = Path(tmp)
        ref, cand = root / "gnu.o", root / "mini.o"
        ref.write_bytes(x86)
        cand.write_bytes(mutate(x86, pos["shdr:1"] + 48,
                                struct.pack("<Q", 8)))
        for extra, expected in (([], 0), (["--strict"], 1)):
            proc = subprocess.run(
                [sys.executable, str(ORACLE), str(ref), str(cand), *extra],
                capture_output=True, text=True, check=False)
            assert proc.returncode == expected, (extra, proc.stdout, proc.stderr)
    print("MINIAS_ORACLE_STRICT_CLI=PASS compatible=0 strict=1")
    print("MINIAS_ORACLE_RELOC_METADATA_ORDER=PASS")


if __name__ == "__main__":
    main()
