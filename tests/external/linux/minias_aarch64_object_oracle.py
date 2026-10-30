#!/usr/bin/env python3
"""Compare the GNU-AS sidecars with the *production* MiniAS Linux objects.

The sidecars never participate in the acceptance link.  This diagnostic is
therefore useful even when the MiniAS Image links but does not boot.
"""

import argparse
from collections import Counter
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import tarfile


# These are deliberate hard limits on *uncompressed* original object bytes.
# The evidence archive is a sample, never an entire Kbuild object-tree dump.
EVIDENCE_MAX_PAIRS = 12
EVIDENCE_MAX_PAIR_BYTES = 512 * 1024
EVIDENCE_MAX_TOTAL_BYTES = 4 * 1024 * 1024
EVIDENCE_DIMENSION_PRIORITY = (
    "contents_masked", "executable_mapping", "global_symbols",
    "sections", "alignment", "relocations_link_effective",
    "relocation_order", "group_semantics", "header",
    "unknown_relocation_fields",
)


def is_transient_probe(path: Path) -> bool:
    """GCC's disposable Kbuild capability checks never become kernel objects.

    The GNU sidecar is retained even after GCC deletes its .tmp_PID/tmp.o
    output.  Comparing that sidecar with the now-absent production pathname
    previously produced 31 false MISSING errors in Linux 6.6.143.
    """
    return (len(path.parts) == 2 and path.name == "tmp.o" and
            re.fullmatch(r"\.tmp_[0-9]+", path.parts[0]) is not None)


def local_link_address(symbol):
    """Normalize linker-equivalent local reference *targets*, not symbols.

    A local STT_FUNC at the beginning of a section and its STT_SECTION
    anchor resolve to the same S+A for an AArch64 relocation. The original
    symbol tables are still checked by the separate strict object oracle.
    Never normalize TLS symbols or external/global bindings this way.
    """
    if not isinstance(symbol, (list, tuple)):
        return symbol
    if (len(symbol) == 7 and symbol[0] == "LOCAL" and
            symbol[1] in ("STT_NOTYPE", "STT_OBJECT", "STT_FUNC") and
            symbol[3] == 0):
        return ("LOCAL_ADDR", symbol[2], symbol[3], symbol[4], symbol[5])
    if len(symbol) == 7 and symbol[0] == "SECTION" and symbol[1] == "STB_LOCAL":
        return ("LOCAL_ADDR", symbol[2], symbol[3], symbol[4], symbol[5])
    return symbol


def allocated_semantics(snapshot):
    """A separate link/runtime projection; never an ELF/CFI/DWARF certificate.

    Keep every SHF_ALLOC section and its masked contents, plus relocation
    records targeting those sections, grouping, visible symbols and machine
    header. DWARF/nonalloc differences remain visible in the strict oracle.
    """
    sections = {name: info for name, info in snapshot["section_core"].items()
                if info["flags"] & 0x2}  # ELF SHF_ALLOC
    names = set(sections)
    # A COMDAT group can contain both loaded code and debug companions.
    # Only the group's signature/flags and SHF_ALLOC members influence
    # the linked runtime image; DWARF-only groups remain strict diagnostics.
    groups = []
    for group in snapshot["groups"]:
        alloc_members = sorted(member for member in group["members"] if member in names)
        if alloc_members:
            groups.append({"signature": group["signature"],
                           "occurrence": group["occurrence"],
                           "flags": group["flags"],
                           "members": alloc_members})
    content = {name: value for name, value in snapshot["contents_masked"].items()
               if name in names}
    align = {name: value for name, value in snapshot["section_alignment"].items()
             if name in names}

    relocs = Counter()
    for row in snapshot["relocations"]:
        target, offset, kind, symbol, addend = row["key"]
        if target not in names:
            continue
        key = (target, offset, kind, local_link_address(symbol), addend)
        relocs[json.dumps(key, separators=(",", ":"))] += row["count"]

    globals_ = Counter()
    for row in snapshot["symbols"]:
        key = row["key"]
        if key[0] == "NAMED":
            globals_[json.dumps(key, separators=(",", ":"))] += row["count"]

    # Only changing mapping boundaries in executable sections can alter
    # the meaning of a disassembler's AArch64 code/data stream. Data-only
    # local mapping symbol differences are reported by the strict oracle.
    exec_sections = {name for name, info in sections.items()
                     if info["flags"] & 0x4}  # ELF SHF_EXECINSTR
    code_mapping = Counter(
        {json.dumps(row["key"], separators=(",", ":")): row["count"]
         for row in snapshot["mapping_symbols"] if row["key"][1] in exec_sections}
    )
    ordered = {key: value for key, value in snapshot["relocation_order"].items()
               if any(key == ".rela" + name or key == ".rel" + name
                      for name in names)}
    unknown = {name: value for name, value in
               snapshot["content_skipped_unknown_reloc_width"].items()
               if name in names}
    return {
        "header": snapshot["header"],
        "sections": sections,
        "contents_masked": content,
        "group_semantics": groups,
        "global_symbols": globals_,
        "relocations_link_effective": relocs,
        "relocation_order": ordered,
        "executable_mapping": code_mapping,
        "alignment": align,
        "unproven_relocation_widths": unknown,
    }


def compare_allocated(oracle, reference_path: Path, candidate_path: Path):
    """Return a fail-closed alloc verdict distinct from strict GNU .o parity."""
    reference = allocated_semantics(oracle.load_semantics(reference_path))
    candidate = allocated_semantics(oracle.load_semantics(candidate_path))
    incompatible = [
        key for key in reference
        if key not in ("alignment", "unproven_relocation_widths") and
        reference[key] != candidate[key]
    ]
    if not oracle.alignment_compatible(
            reference["alignment"], candidate["alignment"]):
        incompatible.append("alignment")
    if (reference["unproven_relocation_widths"] or
            candidate["unproven_relocation_widths"]):
        return "ALLOC_INCONCLUSIVE", ",".join(incompatible + ["unknown_relocation_fields"])
    if incompatible:
        return "ALLOC_DIFF", ",".join(incompatible)
    return "ALLOC_EQUAL", "-"


def write_alloc_diff_evidence(archive_path: Path, gnu_dir: Path, mini_dir: Path,
                              alloc_differences):
    """Preserve bounded, reproducible pairs of *actual* GNU/MiniAS objects.

    Select representative loaded-content, mapping, symbol and relocation
    differences in rotating dimension order. Deterministic sorting prevents
    thousands of relocation-only entries from hiding rarer code-data faults.
    The manifest is a diagnostic, not proof that any difference is harmless.
    """
    records = sorted(alloc_differences, key=lambda item: item[0].as_posix())
    categories = {dimension: [] for dimension in EVIDENCE_DIMENSION_PRIORITY}
    for rel, detail in records:
        present = set(detail.split(","))
        for dimension in EVIDENCE_DIMENSION_PRIORITY:
            if dimension in present:
                categories[dimension].append((rel, detail))
        if not present.intersection(categories):
            categories["unknown_relocation_fields"].append((rel, detail))

    selected = []
    selected_names = set()
    skipped_oversize = set()
    skipped_unsafe = set()
    total_bytes = 0
    while len(selected) < EVIDENCE_MAX_PAIRS:
        made_progress = False
        for dimension in EVIDENCE_DIMENSION_PRIORITY:
            pool = categories[dimension]
            while pool:
                rel, detail = pool.pop(0)
                if rel in selected_names or rel in skipped_oversize or rel in skipped_unsafe:
                    continue
                reference = gnu_dir / rel
                candidate = mini_dir / rel
                if (reference.is_symlink() or candidate.is_symlink() or
                        not reference.is_file() or not candidate.is_file()):
                    skipped_unsafe.add(rel)
                    continue
                pair_bytes = reference.stat().st_size + candidate.stat().st_size
                if pair_bytes > EVIDENCE_MAX_PAIR_BYTES or (
                        total_bytes + pair_bytes > EVIDENCE_MAX_TOTAL_BYTES):
                    skipped_oversize.add(rel)
                    continue
                selected.append((rel, detail, dimension))
                selected_names.add(rel)
                total_bytes += pair_bytes
                made_progress = True
                break
            if len(selected) >= EVIDENCE_MAX_PAIRS:
                break
        if not made_progress:
            break

    manifest = {
        "schema": 1,
        "scope": "AArch64 real Linux 6.6.143 GNU-as sidecar versus MiniAS production",
        "verdict": "DIAGNOSTIC_ONLY_NOT_GNU_EQ_CERTIFICATION",
        "alloc_diff_total": len(records),
        "max_pairs": EVIDENCE_MAX_PAIRS,
        "max_pair_bytes": EVIDENCE_MAX_PAIR_BYTES,
        "max_source_bytes": EVIDENCE_MAX_TOTAL_BYTES,
        "selected_source_bytes": total_bytes,
        "skipped_oversize": len(skipped_oversize),
        "skipped_unsafe": len(skipped_unsafe),
        "objects": [],
    }

    def add_bytes(tar, name, payload):
        member = tarfile.TarInfo(name)
        member.size = len(payload)
        member.mtime = 0
        member.mode = 0o644
        member.uid = member.gid = 0
        member.uname = member.gname = ""
        tar.addfile(member, io.BytesIO(payload))

    archive_path.parent.mkdir(parents=True, exist_ok=True)
    with archive_path.open("wb") as target:
        with gzip.GzipFile(fileobj=target, mode="wb", filename="", mtime=0) as zip_file:
            with tarfile.open(fileobj=zip_file, mode="w") as tar:
                for rel, detail, trigger in selected:
                    item = {"object": rel.as_posix(), "dimensions": detail.split(","),
                            "selected_dimension": trigger}
                    for lane, directory in (("gnu", gnu_dir), ("minias", mini_dir)):
                        data = (directory / rel).read_bytes()
                        # A source changing while we package is not an acceptable
                        # explanation for a potentially truncated ELF sample.
                        if len(data) > EVIDENCE_MAX_PAIR_BYTES:
                            raise RuntimeError(f"evidence source changed: {lane}/{rel}")
                        add_bytes(tar, f"objects/{lane}/{rel.as_posix()}", data)
                        item[lane] = {"sha256": hashlib.sha256(data).hexdigest(),
                                      "bytes": len(data)}
                    manifest["objects"].append(item)
                add_bytes(tar, "manifest.json", (
                    json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode())
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mini-dir", type=Path, required=True)
    parser.add_argument("--gnu-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument(
        "--alloc-diff-evidence", type=Path,
        help="optional bounded deterministic tar.gz of genuine ALLOC_DIFF object pairs",
    )
    parser.add_argument(
        "--require-alloc-parity", action="store_true",
        help="fail if any production object differs in linked SHF_ALLOC semantics",
    )
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location(
        "minias_linux_elf_semantic", root / "assembler/elf_semantic_compare.py"
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("ELF semantic oracle unavailable")
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    dimensions = Counter()
    counts = Counter()
    alloc_counts = Counter()
    alloc_dimensions = Counter()
    alloc_differences = []
    for reference in sorted(args.gnu_dir.rglob("*.o")):
        rel = reference.relative_to(args.gnu_dir)
        if is_transient_probe(rel):
            counts["PROBE_IGNORED"] += 1
            rows.append(("PROBE_IGNORED", rel.as_posix(),
                         "disposable_gcc_capability_probe", "NOT_APPLICABLE", "-"))
            continue
        candidate = args.mini_dir / rel
        alloc_verdict, alloc_detail = "NOT_COMPUTED", "-"
        if not candidate.is_file():
            result = "MISSING"
            detail = "candidate_missing"
            alloc_verdict = "MISSING"
        else:
            try:
                comparison = oracle.compare_paths(reference, candidate)
                failed = [name for name, equal in comparison["dimensions"].items()
                          if not equal]
                if not comparison["strict_equal"]:
                    for name in ("strict_section_alignment", "strict_relaxation_markers",
                                 "strict_mapping_symbols"):
                        if not comparison[name]:
                            failed.append(name)
                result = ("INCONCLUSIVE" if comparison["inconclusive"] else
                          "EQUAL" if comparison["strict_equal"] else "DIFF")
                dimensions.update(failed)
                detail = ",".join(failed) or "-"
                if result == "EQUAL":
                    alloc_verdict, alloc_detail = "ALLOC_EQUAL", "-"
                else:
                    alloc_verdict, alloc_detail = compare_allocated(
                        oracle, reference, candidate)
                alloc_counts[alloc_verdict] += 1
                if alloc_detail != "-":
                    alloc_dimensions.update(alloc_detail.split(","))
                if alloc_verdict == "ALLOC_DIFF":
                    alloc_differences.append((rel, alloc_detail))
            except Exception as exc:
                result = "ORACLE_ERROR"
                detail = str(exc).replace("\t", " ").replace("\n", " ")
                alloc_verdict, alloc_detail = "ORACLE_ERROR", "-"
        counts[result] += 1
        rows.append((result, rel.as_posix(), detail, alloc_verdict, alloc_detail))

    with args.out.open("w") as handle:
        handle.write("result\tobject\tdimensions\talloc_result\talloc_dimensions\n")
        for result, rel, detail, alloc_verdict, alloc_detail in rows:
            handle.write(f"{result}\t{rel}\t{detail}\t{alloc_verdict}\t{alloc_detail}\n")
    if args.alloc_diff_evidence is not None:
        manifest = write_alloc_diff_evidence(
            args.alloc_diff_evidence, args.gnu_dir, args.mini_dir, alloc_differences)
        print("MINIAS_AARCH64_ALLOC_EVIDENCE status=CAPTURED "
              f"total_diff={manifest['alloc_diff_total']} "
              f"pairs={len(manifest['objects'])} "
              f"skipped_oversize={manifest['skipped_oversize']} "
              f"skipped_unsafe={manifest['skipped_unsafe']} "
              f"source_bytes={manifest['selected_source_bytes']} "
              "certified_gnu_equivalence=0")
    print("MINIAS_AARCH64_OBJECT_ORACLE " + " ".join(
        f"{key.lower()}={counts[key]}"
        for key in ("EQUAL", "DIFF", "INCONCLUSIVE", "MISSING", "ORACLE_ERROR",
                    "PROBE_IGNORED")
    ))
    print("MINIAS_AARCH64_ALLOC_ORACLE " + " ".join(
        f"{key.lower()}={alloc_counts[key]}" for key in
        ("ALLOC_EQUAL", "ALLOC_DIFF", "ALLOC_INCONCLUSIVE")
    ))
    if alloc_counts["ALLOC_DIFF"] or alloc_counts["ALLOC_INCONCLUSIVE"]:
        gate_status = ("FAIL" if args.require_alloc_parity else
                       "DIAGNOSTIC_INCOMPLETE")
    else:
        gate_status = "PASS" if not counts["MISSING"] and not counts["ORACLE_ERROR"] else "FAIL"
    print(f"MINIAS_AARCH64_ALLOC_ACCEPTANCE status={gate_status} "
          f"enforced={int(args.require_alloc_parity)} "
          "scope=SHF_ALLOC_plus_link_effective_relocations "
          "debug_dwarf_cfi_parity=NOT_CERTIFIED")
    for dimension, count in alloc_dimensions.most_common():
        print(f"MINIAS_AARCH64_ALLOC_DIMENSION count={count} name={dimension}")
    for dimension, count in dimensions.most_common(12):
        print(f"MINIAS_AARCH64_OBJECT_DIMENSION count={count} name={dimension}")
    for result, rel, detail, _alloc_verdict, _alloc_detail in rows:
        if rel.startswith(("arch/arm64/kernel/", "arch/arm64/mm/")) and result != "EQUAL":
            print(f"MINIAS_AARCH64_EARLY_OBJECT status={result} path={rel} dimensions={detail}")
    production_errors = counts["MISSING"] or counts["ORACLE_ERROR"]
    allocation_errors = (
        alloc_counts["ALLOC_DIFF"] or alloc_counts["ALLOC_INCONCLUSIVE"]
    ) if args.require_alloc_parity else False
    return 1 if production_errors or allocation_errors else 0


if __name__ == "__main__":
    sys.exit(main())
