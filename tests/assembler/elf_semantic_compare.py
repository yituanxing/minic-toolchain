#!/usr/bin/env python3
import argparse
import collections
import json
from pathlib import Path

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

IGNORE_SECTION_NAMES = {".symtab", ".strtab", ".shstrtab"}
R_RISCV_RELAX = 51

# Widths are only used to mask bytes that the linker will rewrite. Unknown
# relocation widths do not become false failures: that section's byte check is
# reported as skipped while relocation semantics are still compared strictly.
RELOC_WIDTH = {
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


def section_name_for_index(elf, idx):
    if isinstance(idx, int):
        if 0 <= idx < elf.num_sections():
            return elf.get_section(idx).name
        return f"INDEX:{idx}"
    return str(idx)


def symbol_semantic_key(elf, sym):
    info = sym["st_info"]
    bind = info["bind"]
    typ = info["type"]
    vis = sym["st_other"]["visibility"]
    shndx = sym["st_shndx"]
    sec = section_name_for_index(elf, shndx)
    value = int(sym["st_value"])
    size = int(sym["st_size"])
    name = sym.name or ""

    if typ == "STT_FILE":
        return None
    if typ == "STT_SECTION":
        return ("SECTION", sec)
    # GNU as and MiniAS may invent different local anchor names for an
    # equivalent PC-relative relocation pair. Normalize defined locals by the
    # semantic address they denote instead of their private spelling.
    if bind == "STB_LOCAL" and isinstance(shndx, int):
        return ("LOCAL", typ, vis, sec, value, size)
    return ("NAMED", name, bind, typ, vis, sec, value, size)


def symbol_counter(elf):
    out = collections.Counter()
    symtab = elf.get_section_by_name(".symtab")
    if symtab is None:
        return out
    for sym in symtab.iter_symbols():
        key = symbol_semantic_key(elf, sym)
        if key is not None:
            out[key] += 1
    return out


def relocation_counter(elf):
    out = collections.Counter()
    masks = collections.defaultdict(list)
    unknown_width = collections.defaultdict(list)

    for sec in elf.iter_sections():
        if not isinstance(sec, RelocationSection):
            continue
        target_name = section_name_for_index(elf, int(sec["sh_info"]))
        symtab = elf.get_section(int(sec["sh_link"]))
        is_rela = sec.is_RELA()

        for rel in sec.iter_relocations():
            typ = int(rel["r_info_type"])
            offset = int(rel["r_offset"])
            addend = int(rel["r_addend"]) if is_rela else 0
            if typ == R_RISCV_RELAX:
                # RELAX is a linker optimization hint, not required for the
                # unrelaxed object's functional semantics.
                continue

            sym = symtab.get_symbol(int(rel["r_info_sym"]))
            sym_key = symbol_semantic_key(elf, sym)
            if sym_key is None:
                sym_key = ("NONE",)
            out[(target_name, offset, typ, sym_key, addend)] += 1

            width = RELOC_WIDTH.get(typ)
            if width:
                masks[target_name].append((offset, offset + width))
            else:
                unknown_width[target_name].append((offset, typ))

    return out, masks, unknown_width


def section_metadata(elf):
    out = {}
    for sec in elf.iter_sections():
        name = sec.name
        if name in IGNORE_SECTION_NAMES or isinstance(sec, RelocationSection):
            continue
        hdr = sec.header
        out[name] = {
            "type": str(hdr["sh_type"]),
            "flags": int(hdr["sh_flags"]),
            "align": int(hdr["sh_addralign"]),
            "entsize": int(hdr["sh_entsize"]),
            "size": int(hdr["sh_size"]),
        }
    return out


def masked_bytes(data, ranges):
    b = bytearray(data)
    for start, end in ranges:
        start = max(0, min(len(b), start))
        end = max(start, min(len(b), end))
        b[start:end] = b"\x00" * (end - start)
    return bytes(b)


def comparable_contents(elf, masks, unknown_width):
    out = {}
    skipped = {}
    for sec in elf.iter_sections():
        name = sec.name
        if name in IGNORE_SECTION_NAMES or isinstance(sec, RelocationSection):
            continue
        if str(sec["sh_type"]) != "SHT_PROGBITS":
            continue
        if unknown_width.get(name):
            skipped[name] = list(unknown_width[name])
            continue
        out[name] = masked_bytes(sec.data(), masks.get(name, [])).hex()
    return out, skipped


def counter_to_sorted_list(counter):
    rows = [{"key": key, "count": count} for key, count in counter.items()]
    rows.sort(key=lambda row: json.dumps(row["key"], sort_keys=True, default=str))
    return rows


def load_semantics(path):
    with open(path, "rb") as f:
        elf = ELFFile(f)
        relocs, masks, unknown = relocation_counter(elf)
        contents, skipped = comparable_contents(elf, masks, unknown)
        return {
            "header": {
                "class": int(elf.elfclass),
                "little_endian": bool(elf.little_endian),
                "type": str(elf.header["e_type"]),
                "machine": str(elf.header["e_machine"]),
                "flags": int(elf.header["e_flags"]),
            },
            "sections": section_metadata(elf),
            "symbols": counter_to_sorted_list(symbol_counter(elf)),
            "relocations": counter_to_sorted_list(relocs),
            "contents_masked": contents,
            "content_skipped_unknown_reloc_width": skipped,
        }


def main():
    parser = argparse.ArgumentParser(
        description="Compare RISC-V ELF relocatable objects semantically."
    )
    parser.add_argument("reference")
    parser.add_argument("candidate")
    parser.add_argument("--json-out")
    args = parser.parse_args()

    reference = load_semantics(args.reference)
    candidate = load_semantics(args.candidate)
    dimensions = {
        key: reference[key] == candidate[key]
        for key in ("header", "sections", "symbols", "relocations", "contents_masked")
    }

    result = {
        "reference": str(Path(args.reference)),
        "candidate": str(Path(args.candidate)),
        "equal": all(dimensions.values()),
        "dimensions": dimensions,
        "reference_skipped_content": reference["content_skipped_unknown_reloc_width"],
        "candidate_skipped_content": candidate["content_skipped_unknown_reloc_width"],
    }
    if not result["equal"]:
        result["differences"] = {
            key: {"reference": reference[key], "candidate": candidate[key]}
            for key, equal in dimensions.items()
            if not equal
        }

    output = json.dumps(result, indent=2, sort_keys=True)
    if args.json_out:
        Path(args.json_out).write_text(output + "\n")
    print(output)
    raise SystemExit(0 if result["equal"] else 1)


if __name__ == "__main__":
    main()
