#!/usr/bin/env python3
import argparse
import collections
import hashlib
import json
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

IGNORE_SECTION_NAMES = {".symtab", ".strtab", ".shstrtab"}
R_RISCV_ALIGN = 43
R_RISCV_RELAX = 51

# A relocation may rewrite *bits of an instruction*, not all bytes occupied
# by that instruction. Masking the whole R_RISCV_BRANCH word would silently
# equate BEQ and BNE; masking an AArch64 ADRP word would hide its destination
# register. Every entry below is a tuple of (byte displacement, field width
# in bytes, bit mask inside the field). The mask names ONLY linker-modified
# bits. Types absent from a machine table make the verdict INCONCLUSIVE.
#
# Sources: RISC-V ELF psABI §Relocations, ARM AAELF64 §5.7, LoongArch ELF ABI
# §Relocations, and the x86-64 ELF ABI relocation definitions in elf.h.
# https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-elf.adoc
# https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst
# https://github.com/loongson/LoongArch-Documentation/blob/main/docs/LoongArch-ELF-ABI-EN.adoc
def data_field(size):
    return ((0, size, (1 << (size * 8)) - 1),)


def instruction_field(mask):
    return ((0, 4, mask),)


RV_U = instruction_field(0xfffff000)
RV_I = instruction_field(0xfff00000)
RV_S = instruction_field(0xfe000f80)
RV_B = RV_S  # B-type and S-type scatter to the same instruction bit positions.
RV_J = instruction_field(0xfffff000)
RV_CALL = ((0, 4, 0xfffff000), (4, 4, 0xfff00000))
LA_LO12 = instruction_field(0x003ffc00)
LA_HI20 = instruction_field(0x01ffffe0)

RISC_V_RELOC_FIELDS = {
    0: (), 1: data_field(4), 2: data_field(8),
    **{t: RV_B for t in (16,)},
    17: RV_J, 18: RV_CALL, 19: RV_CALL,
    **{t: RV_U for t in (20, 21, 22, 23, 26, 29, 62)},
    **{t: RV_I for t in (24, 27, 30, 63, 64)},
    **{t: RV_S for t in (25, 28, 31)},
    32: (),  # TPREL_ADD annotates ADD: the linker changes no instruction bits.
    **{t: data_field(n) for t, n in {
        33: 1, 34: 2, 35: 4, 36: 8,
        37: 1, 38: 2, 39: 4, 40: 8,
        41: 4, 54: 1, 55: 2, 56: 4, 57: 4, 59: 4,
    }.items()},
    44: ((0, 2, 0x1c7c),),  # RVC CB-Type immediate, retain rs1'/opcode.
    45: ((0, 2, 0x1ffc),),  # RVC CJ-Type immediate, retain funct3/opcode.
    52: ((0, 1, 0x3f),), 53: ((0, 1, 0x3f),),  # SUB6 / SET6.
    65: (),  # TLSDESC_CALL is a relaxation marker, not a patched field.
}

X86_RELOC_FIELDS = {
    0: (),
    **{t: data_field(n) for t, n in {
        1: 8, 2: 4, 3: 4, 4: 4, 9: 4, 10: 4, 11: 4,
        12: 2, 13: 2, 14: 1, 15: 1,
        16: 8, 17: 8, 18: 8, 19: 4, 20: 4, 21: 4, 22: 4, 23: 4,
        24: 8, 25: 8, 26: 4, 27: 8, 28: 8, 29: 8, 30: 8, 31: 8,
        32: 4, 33: 8, 34: 4, 36: 8, 41: 4, 42: 4,
    }.items()},
    35: (),  # R_X86_64_TLSDESC_CALL marker; preserve the complete CALL.
}

A64_MOVW = instruction_field(0x001fffe0)  # 16-bit immediate at [20:5].
A64_SIGNED_MOVW = instruction_field(0x401fffe0)  # MOVN/MOVZ selection too.
A64_ADR = instruction_field(0x60ffffe0)  # immlo [30:29], immhi [23:5].
A64_LO12 = instruction_field(0x003ffc00)  # imm12 [21:10].
AARCH64_RELOC_FIELDS = {
    0: (), 256: (),
    **{t: data_field(n) for t, n in {
        257: 8, 258: 4, 259: 2, 260: 8, 261: 4, 262: 2, 314: 4,
    }.items()},
    **{t: A64_MOVW for t in (263, 264, 265, 266, 267, 268, 269,
                             288, 290)},
    **{t: A64_SIGNED_MOVW for t in (270, 271, 272, 287, 289, 291)},
    273: instruction_field(0x00ffffe0),  # LDR literal [23:5].
    274: A64_ADR, 275: A64_ADR, 276: A64_ADR,
    277: A64_LO12, 278: A64_LO12,
    279: instruction_field(0x0007ffe0),  # test-and-branch imm14 [18:5].
    280: instruction_field(0x00ffffe0),  # conditional branch imm19 [23:5].
    282: instruction_field(0x03ffffff),
    283: instruction_field(0x03ffffff),
    **{t: A64_LO12 for t in (284, 285, 286, 299)},
}

LOONGARCH_RELOC_FIELDS = {
    0: (), 1: data_field(4), 2: data_field(8),
    6: data_field(4), 7: data_field(8), 8: data_field(4),
    9: data_field(8), 10: data_field(4), 11: data_field(8),
    **{t: () for t in range(20, 38)},
    38: instruction_field(0x00007c00),
    **{t: LA_LO12 for t in (39, 40, 68, 70, 72, 74, 76, 78, 80, 82, 84)},
    41: instruction_field(0x03fffc00),
    42: instruction_field(0x03fffc00),
    43: LA_HI20,
    44: instruction_field(0x03fffc1f),
    45: instruction_field(0x03ffffff),
    46: data_field(4),
    **{t: data_field(n) for t, n in {
        47: 1, 48: 2, 49: 3, 50: 4, 51: 8,
        52: 1, 53: 2, 54: 3, 55: 4, 56: 8,
    }.items()},
    57: (), 58: (),
    64: instruction_field(0x03fffc00),
    65: instruction_field(0x03fffc1f),
    66: instruction_field(0x03ffffff),
    **{t: LA_HI20 for t in (67, 69, 71, 73, 75, 77, 79, 81, 83)},
}

RELOC_FIELDS_BY_MACHINE = {
    "EM_RISCV": RISC_V_RELOC_FIELDS,
    "EM_X86_64": X86_RELOC_FIELDS,
    "EM_AARCH64": AARCH64_RELOC_FIELDS,
    "EM_LOONGARCH": LOONGARCH_RELOC_FIELDS,
}


def section_name_for_index(elf, idx, section_keys):
    if isinstance(idx, int):
        if 0 <= idx < elf.num_sections():
            return section_keys[idx]
        return f"INDEX:{idx}"
    return str(idx)


def stable_sections(elf):
    """Give duplicate names distinct identities, including COMDAT groups.

    ELF section indices and .group payload indices differ between assemblers.
    Group *signatures* and section occurrence are the stable object semantics.
    No ordinary dict indexed only by `section.name` can represent GNU groups.
    """
    group_owners = {}
    group_info = []
    signature_occurrence = collections.Counter()
    endian = "<" if elf.little_endian else ">"
    for index, section in enumerate(elf.iter_sections()):
        if str(section["sh_type"]) != "SHT_GROUP":
            continue
        raw = section.data()
        if len(raw) < 4 or len(raw) % 4:
            raise ValueError("malformed SHT_GROUP member table")
        words = struct.unpack(endian + "I" * (len(raw) // 4), raw)
        symbol_table = elf.get_section(int(section["sh_link"]))
        if symbol_table is None or not hasattr(symbol_table, "get_symbol"):
            raise ValueError("SHT_GROUP missing signature symtab")
        signature = symbol_table.get_symbol(int(section["sh_info"])).name
        ordinal = signature_occurrence[signature]
        signature_occurrence[signature] += 1
        owner = (signature, ordinal)
        group_owners[index] = owner
        members = []
        for member in words[1:]:
            if member <= 0 or member >= elf.num_sections() or member in group_owners:
                raise ValueError("SHT_GROUP invalid or overlapping member")
            group_owners[member] = owner
            members.append(member)
        group_info.append((owner, words[0], members))

    # Every section receives an identity; duplicate ungrouped names retain a
    # deterministic occurrence and group members identify their signature.
    counts = collections.Counter(
        (sec.name, group_owners.get(idx))
        for idx, sec in enumerate(elf.iter_sections()))
    occurrences = collections.Counter()
    keys = {}
    for idx, sec in enumerate(elf.iter_sections()):
        base = (sec.name, group_owners.get(idx))
        ordinal = occurrences[base]
        occurrences[base] += 1
        if counts[base] == 1 and base[1] is None:
            keys[idx] = sec.name  # Preserve familiar identity for normal ELF.
        else:
            keys[idx] = json.dumps([sec.name, base[1], ordinal],
                                   separators=(",", ":"))
    groups = []
    for owner, flags, members in group_info:
        groups.append({"signature": owner[0], "occurrence": owner[1],
                       "flags": flags, "members": sorted(keys[i] for i in members)})
    groups.sort(key=lambda row: json.dumps(row, sort_keys=True))
    return keys, groups


def symbol_semantic_key(elf, sym, section_keys):
    info = sym["st_info"]
    bind = info["bind"]
    typ = info["type"]
    vis = sym["st_other"]["visibility"]
    shndx = sym["st_shndx"]
    sec = section_name_for_index(elf, shndx, section_keys)
    value = int(sym["st_value"])
    size = int(sym["st_size"])
    name = sym.name or ""

    if typ == "STT_FILE":
        return ("FILE", name, sec)
    if bind == "STB_LOCAL" and (name.startswith("$x") or name.startswith("$d")):
        # RISC-V mapping symbols are assembler/disassembler metadata, not
        # link-visible source symbols.
        return None
    if typ == "STT_SECTION":
        return ("SECTION", sec)
    # GNU as and MiniAS may invent different local anchor names for an
    # equivalent PC-relative relocation pair. Normalize defined locals by the
    # semantic address they denote instead of their private spelling.
    if bind == "STB_LOCAL" and isinstance(shndx, int):
        return ("LOCAL", typ, vis, sec, value, size)
    return ("NAMED", name, bind, typ, vis, sec, value, size)


def symbol_counter(elf, section_keys):
    out = collections.Counter()
    symtab = elf.get_section_by_name(".symtab")
    if symtab is None:
        return out
    for sym in symtab.iter_symbols():
        key = symbol_semantic_key(elf, sym, section_keys)
        if key is not None:
            out[key] += 1
    return out


def relocation_counter(elf, section_keys):
    out = collections.Counter()
    masks = collections.defaultdict(list)
    unknown_width = collections.defaultdict(list)

    machine = str(elf.header["e_machine"])
    fields = RELOC_FIELDS_BY_MACHINE.get(machine, {})
    for sec in elf.iter_sections():
        if not isinstance(sec, RelocationSection):
            continue
        target_name = section_name_for_index(elf, int(sec["sh_info"]), section_keys)
        symtab = elf.get_section(int(sec["sh_link"]))
        is_rela = sec.is_RELA()

        for rel in sec.iter_relocations():
            typ = int(rel["r_info_type"])
            offset = int(rel["r_offset"])
            addend = int(rel["r_addend"]) if is_rela else 0
            if not is_rela:
                unknown_width[target_name].append(
                    (offset, typ, "REL-implicit-addend-not-decoded"))
            if machine == "EM_RISCV" and typ == R_RISCV_RELAX:
                # RELAX is a linker optimization hint, not required for the
                # unrelaxed object's functional semantics.
                continue
            if machine == "EM_RISCV" and typ == R_RISCV_ALIGN:
                # GNU as may reserve relaxable alignment padding and let the
                # linker delete excess bytes. Treat only verified NOP bytes
                # as linker-discardable: masking arbitrary bytes here would
                # hide a changed instruction in executable code.
                if addend > 0:
                    masks[target_name].append((offset, addend, None))
                continue

            sym = symtab.get_symbol(int(rel["r_info_sym"]))
            sym_key = symbol_semantic_key(elf, sym, section_keys)
            if sym_key is None:
                sym_key = ("NONE",)
            out[(target_name, offset, typ, sym_key, addend)] += 1

            # ELF REL records its addend in the section bytes, not r_addend.
            # Without decoding its target-specific field, treating REL as a
            # zero-addend RELA could call two different relocations equal.
            if not is_rela:
                continue
            if typ in fields:
                masks[target_name].extend(
                    (offset + displacement, width, bitmask)
                    for displacement, width, bitmask in fields[typ])
            else:
                unknown_width[target_name].append(
                    (offset, typ, "unknown-target-relocation-fields"))

    return out, masks, unknown_width


def section_metadata(elf, section_keys):
    core = {}
    alignment = {}
    for index, sec in enumerate(elf.iter_sections()):
        name = sec.name
        if name in IGNORE_SECTION_NAMES or isinstance(sec, RelocationSection):
            continue
        hdr = sec.header
        key = section_keys[index]
        metadata = {
            "type": str(hdr["sh_type"]),
            "flags": int(hdr["sh_flags"]),
            "entsize": int(hdr["sh_entsize"]),
            "size": int(hdr["sh_size"]),
        }
        # SHF_LINK_ORDER's sh_link refers to the associated input section,
        # not to the symbol table like SHT_RELA's sh_link. Compare the
        # section's semantic identity instead of its layout-dependent ELF
        # header number; two otherwise identical patch-table sections may
        # have different retention rules under --gc-sections.
        if int(hdr["sh_flags"]) & 0x80:
            linked = int(hdr["sh_link"])
            if linked < 0 or linked >= elf.num_sections():
                raise ValueError("SHF_LINK_ORDER section links outside ELF")
            metadata["linked_section"] = section_keys[linked] if linked else None
        core[key] = metadata
        alignment[key] = int(hdr["sh_addralign"])
    return core, alignment


def alignment_compatible(reference, candidate):
    if set(reference) != set(candidate):
        return False
    for name, ref_align in reference.items():
        cand_align = candidate[name]
        if ref_align == 0:
            if cand_align != 0:
                return False
            continue
        if cand_align < ref_align or cand_align % ref_align != 0:
            return False
    return True


def riscv_alignment_nops(data, little_endian):
    """The only NOP encodings GAS uses for R_RISCV_ALIGN padding."""
    byteorder = "little" if little_endian else "big"
    offset = 0
    while offset < len(data):
        if (offset + 4 <= len(data) and
                int.from_bytes(data[offset:offset + 4], byteorder) == 0x00000013):
            offset += 4  # addi zero,zero,0
        elif (offset + 2 <= len(data) and
              int.from_bytes(data[offset:offset + 2], byteorder) == 0x0001):
            offset += 2  # c.nop
        else:
            return False
    return True


def masked_bytes(data, ranges, little_endian):
    b = bytearray(data)
    byteorder = "little" if little_endian else "big"
    for start, width, bitmask in ranges:
        end = start + width
        if start < 0 or width < 0 or end > len(b):
            raise ValueError("relocation field extends outside target section")
        if bitmask is None:
            # The only whole-range suppression is an R_RISCV_ALIGN area,
            # whose original bytes MUST first be verified as legal NOPs.
            if not riscv_alignment_nops(data[start:end], little_endian):
                raise ValueError("R_RISCV_ALIGN covers non-NOP source bytes")
            b[start:end] = b"\x00" * width
            continue
        if bitmask < 0 or bitmask >= (1 << (8 * width)):
            raise ValueError("invalid target relocation bit mask")
        value = int.from_bytes(b[start:end], byteorder)
        b[start:end] = (value & ~bitmask).to_bytes(width, byteorder)
    return bytes(b)


def comparable_contents(elf, section_keys, masks, unknown_width):
    out = {}
    skipped = {}
    for index, sec in enumerate(elf.iter_sections()):
        name = sec.name
        if name in IGNORE_SECTION_NAMES or isinstance(sec, RelocationSection):
            continue
        kind = str(sec["sh_type"])
        if kind in ("SHT_NOBITS", "SHT_NULL", "SHT_GROUP"):
            # NOBITS has no file-backed content; GROUP is normalized above
            # by signature and membership, not raw section-header indices.
            continue
        key = section_keys[index]
        if kind == "SHT_SYMTAB_SHNDX":
            # This table encodes ELF section indices tied to symtab order.
            # Until it is symbolically normalized, never assert equality.
            skipped[key] = [(0, "SHT_SYMTAB_SHNDX", "unsupported-index-table")]
            continue
        if unknown_width.get(key):
            skipped[key] = list(unknown_width[key])
            continue
        # Include SHT_NOTE, init/fini/preinit arrays, RISC-V attributes and
        # all other file-backed section types. Type and size alone cannot
        # certify their contents; many affect loader/linker behavior.
        data = masked_bytes(sec.data(), masks.get(key, []), elf.little_endian)
        out[key] = {
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
    return out, skipped


def counter_to_sorted_list(counter):
    rows = [{"key": key, "count": count} for key, count in counter.items()]
    rows.sort(key=lambda row: json.dumps(row["key"], sort_keys=True, default=str))
    return rows


def load_semantics(path):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        section_keys, groups = stable_sections(elf)
        relocs, masks, unknown = relocation_counter(elf, section_keys)
        contents, skipped = comparable_contents(elf, section_keys, masks, unknown)
        section_core, section_alignment = section_metadata(elf, section_keys)
        return {
            "header": {
                "class": int(elf.elfclass),
                "little_endian": bool(elf.little_endian),
                "type": str(elf.header["e_type"]),
                "machine": str(elf.header["e_machine"]),
                "flags": int(elf.header["e_flags"]),
            },
            "section_core": section_core,
            "section_alignment": section_alignment,
            "groups": groups,
            "symbols": counter_to_sorted_list(symbol_counter(elf, section_keys)),
            "relocations": counter_to_sorted_list(relocs),
            "contents_masked": contents,
            "content_skipped_unknown_reloc_width": skipped,
        }


def compare_paths(reference_path, candidate_path):
    reference = load_semantics(reference_path)
    candidate = load_semantics(candidate_path)
    dimensions = {
        "header": reference["header"] == candidate["header"],
        "section_core": reference["section_core"] == candidate["section_core"],
        "groups": reference["groups"] == candidate["groups"],
        "section_alignment_compatible": alignment_compatible(
            reference["section_alignment"], candidate["section_alignment"]
        ),
        "symbols": reference["symbols"] == candidate["symbols"],
        "relocations": reference["relocations"] == candidate["relocations"],
        "contents_masked": reference["contents_masked"] == candidate["contents_masked"],
        "known_relocation_widths": not (
            reference["content_skipped_unknown_reloc_width"] or
            candidate["content_skipped_unknown_reloc_width"]),
    }
    strict_alignment = reference["section_alignment"] == candidate["section_alignment"]

    compatible_equal = all(dimensions.values())
    result = {
        "reference": str(Path(reference_path)),
        "candidate": str(Path(candidate_path)),
        "equal": compatible_equal,  # Historical compatible-alignment verdict.
        "compatible_equal": compatible_equal,
        "strict_equal": compatible_equal and strict_alignment,
        "alignment_verdict": ("exact" if strict_alignment else
                              "compatible" if dimensions["section_alignment_compatible"]
                              else "incompatible"),
        "inconclusive": not dimensions["known_relocation_widths"],
        "dimensions": dimensions,
        "strict_section_alignment": strict_alignment,
        "reference_skipped_content": reference["content_skipped_unknown_reloc_width"],
        "candidate_skipped_content": candidate["content_skipped_unknown_reloc_width"],
    }
    if not result["equal"] or not strict_alignment:
        differences = {}
        for key, equal in dimensions.items():
            if not equal and key not in ("section_alignment_compatible",
                                         "known_relocation_widths"):
                ref_value = reference[key]
                cand_value = candidate[key]
                differences[key] = {
                    "reference_count": len(ref_value) if hasattr(ref_value, "__len__") else None,
                    "candidate_count": len(cand_value) if hasattr(cand_value, "__len__") else None,
                    "reference_sample": list(ref_value.items())[:20] if isinstance(ref_value, dict) else ref_value[:20],
                    "candidate_sample": list(cand_value.items())[:20] if isinstance(cand_value, dict) else cand_value[:20],
                }
        if not dimensions["section_alignment_compatible"] or not strict_alignment:
            differences["section_alignment"] = {
                "reference": reference["section_alignment"],
                "candidate": candidate["section_alignment"],
                "compatible": dimensions["section_alignment_compatible"],
            }
        if not dimensions["known_relocation_widths"]:
            differences["unknown_relocation_widths"] = {
                "reference": reference["content_skipped_unknown_reloc_width"],
                "candidate": candidate["content_skipped_unknown_reloc_width"],
            }
        result["differences"] = differences
    return result


def main():
    parser = argparse.ArgumentParser(
        description="Compare ELF relocatable objects, including COMDAT identities."
    )
    parser.add_argument("reference")
    parser.add_argument("candidate")
    parser.add_argument("--json-out")
    args = parser.parse_args()

    result = compare_paths(args.reference, args.candidate)
    output = json.dumps(result, indent=2, sort_keys=True)
    if args.json_out:
        Path(args.json_out).write_text(output + "\n")
    print(output)
    raise SystemExit(0 if result["equal"] else 1)


if __name__ == "__main__":
    main()
