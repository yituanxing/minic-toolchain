#!/usr/bin/env python3
"""Evidence-only RV64 generated-assembly instruction census.

Use the pinned native Stage4 GitHub artifact ZIP, not a recompiled replacement:
  python3 tools/ci/minic-native-asm-instruction-census-v1.py \
    --artifact-zip build/stage4-native.zip --output build/stage4-asm-census.json

Counts *assembly mnemonic lines* rather than executed instructions or cycles.
Pseudo-ops may expand and hot loops weigh more than static code.  This tool
does not modify emitted assembly, the compiler, the current Linux build, or
the trusted Stage3/Stage4 fixedpoint.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import zipfile

INSTRUCTION = re.compile(r"^\s+([a-z][a-z0-9.]*)\s+([^#\r\n]*\S)\s*$")
STACK_ARG = re.compile(r"^(?:t[0-6]|a[0-7]|s(?:[0-9]|10|11)), -?\d+\(sp\)$")
LOADS = {"lb", "lbu", "lh", "lhu", "lw", "lwu", "ld", "flw", "fld"}
STORES = {"sb", "sh", "sw", "sd", "fsw", "fsd"}
STACK_RELOAD_PAIRS = {"sd": "ld", "sw": "lw", "sh": "lh", "sb": "lb"}
ALL_OPS = LOADS | STORES
ELF_SHA = "50b3f7763bde2505e17bb545513cb00664286d4e762b3395aea3f29ce724ef31"


def census(name: str, source: bytes) -> dict:
    insns: list[tuple[str, str]] = []
    for line in source.decode("utf-8").splitlines():
        match = INSTRUCTION.match(line)
        if match:
            insns.append((match.group(1), match.group(2).strip()))
    histogram = Counter(op for op, _arg in insns)
    candidate_count = sum(
        1
        for (op, arg), (next_op, next_arg) in zip(insns, insns[1:])
        if STACK_RELOAD_PAIRS.get(op) == next_op
        and arg == next_arg
        and STACK_ARG.fullmatch(arg)
    )
    memory = sum(histogram[op] for op in ALL_OPS)
    return {
        "source": name,
        "assembly_sha256": hashlib.sha256(source).hexdigest(),
        "mnemonics": len(insns),
        "load_store_mnemonics": memory,
        "load_store_fraction": round(memory / len(insns), 6) if insns else 0,
        "adjacent_same_register_stack_store_reload": candidate_count,
        "top_mnemonics": histogram.most_common(15),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--artifact-zip", type=Path)
    source.add_argument("--artifact-dir", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--min-sources", type=int, default=77)
    args = parser.parse_args()
    rows = []
    root = "full-native/stage4/s/"
    elf = "full-native/stage4/bin/minic-cc"
    archive = zipfile.ZipFile(args.artifact_zip) if args.artifact_zip else None
    try:
        if archive is not None:
            members = archive.namelist()
            read = archive.read
        else:
            base = args.artifact_dir.resolve()
            members = [str(p.relative_to(base).as_posix()) for p in base.rglob("*") if p.is_file()]
            read = lambda member: (base / member).read_bytes()
        assembly = sorted(x for x in members if x.startswith(root) and x.endswith(".s"))
        if len(assembly) != args.min_sources:
            raise SystemExit(f"incorrect production source count: {len(assembly)} != {args.min_sources}")
        binary_sha = hashlib.sha256(read(elf)).hexdigest()
        if binary_sha != ELF_SHA:
            raise SystemExit("uncertified native Stage4 compiler SHA256: " + binary_sha)
        for member in assembly:
            row = census(member[len(root):], read(member))
            if row["mnemonics"] == 0:
                raise SystemExit("zero machine instruction mnemonics for " + member)
            rows.append(row)
    finally:
        if archive is not None:
            archive.close()
    total = sum(row["mnemonics"] for row in rows)
    memory = sum(row["load_store_mnemonics"] for row in rows)
    candidates = sum(row["adjacent_same_register_stack_store_reload"] for row in rows)
    result = {
        "schema": "minic-stage4-asm-static-instruction-census-v1",
        "origin": "full-native/stage4/s/ and bin/minic-cc of CI 39058935623",
        "compiler_elf_sha256": binary_sha,
        "source_count": len(rows),
        "assembly_mnemonics": total,
        "load_store_mnemonics": memory,
        "load_store_fraction": round(memory / total, 6),
        "adjacent_same_register_stack_store_reload": candidates,
        "candidate_fraction_of_assembly_mnemonics": round(candidates / total, 6),
        "notes": [
            "Static assembly mnemonics, NOT dynamic executed instruction/cycle counts.",
            "RISC-V pseudo-instructions and compression mean the real binary instruction count may differ.",
            "Adjacent pairs are optimization candidates only; actual code changes need runtime regression and full Stage4/Linux retest.",
            "This audit does not imply insufficient RV64 ISA instruction availability.",
        ],
        "sources": sorted(rows, key=lambda x: x["mnemonics"], reverse=True),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"MINIC_NATIVE_ASM_CENSUS=PASS sources={len(rows)} mnemonics={total}"
          f" loads_stores={memory} load_store_fraction={memory/total:.4f}"
          f" redundant_stack_pairs={candidates} pairs_per_100_mnemonics={100*candidates/total:.2f}")
    for row in sorted(rows, key=lambda x: x["adjacent_same_register_stack_store_reload"], reverse=True)[:10]:
        print("MINIC_NATIVE_ASM_HOTSTATIC source=%s mnemonics=%d memory=%d redundant_pairs=%d" %
              (row["source"], row["mnemonics"], row["load_store_mnemonics"],
               row["adjacent_same_register_stack_store_reload"]))


if __name__ == "__main__":
    main()
