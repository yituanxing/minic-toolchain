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

# Widths mask only fields the linker will rewrite. The numeric relocation
# namespace is machine-specific: a RISC-V type number is not an x86/ARM type.
# Unrecognized widths make the strict verdict INCONCLUSIVE, never equal.
RISC_V_RELOC_WIDTH = {
    1: 4, 2: 8,
    16: 4, 17: 4, 18: 8, 19: 8,
    20: 4, 21: 4, 22: 4, 23: 4, 24: 4, 25: 4,
    26: 4, 27: 4, 28: 4, 29: 4, 30: 4, 31: 4, 32: 4,
    33: 1, 34: 2, 35: 4, 36: 8,
    37: 1, 38: 2, 39: 4, 40: 8,
    44: 2, 45: 2, 46: 2,
    47: 4, 48: 4, 49: 4, 50: 4,
    52: 1, 53: 1, 54: 1, 55: 2, 56: 4, 57: 4,
    59: 4, 62: 4, 63: 4, 64: 4, 65: 4,
}
RELOC_WIDTH_BY_MACHINE = {
    "EM_RISCV": RISC_V_RELOC_WIDTH,
    "EM_X86_64": {1: 8, 2: 4, 3: 4, 4: 4, 9: 4, 10: 4,
                  11: 4, 12: 2, 13: 2, 14: 1, 15: 1, 24: 8,
                  25: 8, 26: 4, 27: 8, 28: 8, 32: 4, 33: 8,
                  34: 4, 35: 4, 36: 8, 41: 4, 42: 4},
    # Grow these from GNU external oracles, not by guessing a relocation's
    # field width. All other A64/Loong relocations are explicitly unknown.
    "EM_AARCH64": {257: 8, 258: 4, 259: 2, 260: 8, 261: 4, 262: 2,
                   275: 4, 277: 4, 278: 4, 282: 4, 283: 4},
    "EM_LOONGARCH": {1: 1, 2: 2, 3: 4, 4: 8, 5: 4, 6: 8},
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
    widths = RELOC_WIDTH_BY_MACHINE.get(machine, {})
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
            if machine == "EM_RISCV" and typ == R_RISCV_RELAX:
                # RELAX is a linker optimization hint, not required for the
                # unrelaxed object's functional semantics.
                continue
            if machine == "EM_RISCV" and typ == R_RISCV_ALIGN:
                # GNU as may reserve relaxable alignment padding and let the
                # linker delete excess bytes. MiniAS currently commits the
                # final padding in the object, so ALIGN is optional metadata.
                if addend > 0:
                    masks[target_name].append((offset, offset + addend))
                continue

            sym = symtab.get_symbol(int(rel["r_info_sym"]))
            sym_key = symbol_semantic_key(elf, sym, section_keys)
            if sym_key is None:
                sym_key = ("NONE",)
            out[(target_name, offset, typ, sym_key, addend)] += 1

            width = widths.get(typ)
            if width:
                masks[target_name].append((offset, offset + width))
            else:
                unknown_width[target_name].append((offset, typ))

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


def masked_bytes(data, ranges):
    b = bytearray(data)
    for start, end in ranges:
        start = max(0, min(len(b), start))
        end = max(start, min(len(b), end))
        b[start:end] = b"\x00" * (end - start)
    return bytes(b)


def comparable_contents(elf, section_keys, masks, unknown_width):
    out = {}
    skipped = {}
    for index, sec in enumerate(elf.iter_sections()):
        name = sec.name
        if name in IGNORE_SECTION_NAMES or isinstance(sec, RelocationSection):
            continue
        if str(sec["sh_type"]) != "SHT_PROGBITS":
            continue
        key = section_keys[index]
        if unknown_width.get(key):
            skipped[key] = list(unknown_width[key])
            continue
        data = masked_bytes(sec.data(), masks.get(key, []))
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

    result = {
        "reference": str(Path(reference_path)),
        "candidate": str(Path(candidate_path)),
        "equal": all(dimensions.values()),
        "strict_equal": all(dimensions.values()) and strict_alignment,
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
