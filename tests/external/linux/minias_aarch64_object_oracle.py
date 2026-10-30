#!/usr/bin/env python3
"""Compare the GNU-AS sidecars with the *production* MiniAS Linux objects.

The sidecars never participate in the acceptance link.  This diagnostic is
therefore useful even when the MiniAS Image links but does not boot.
"""

import argparse
from collections import Counter
import importlib.util
from pathlib import Path
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mini-dir", type=Path, required=True)
    parser.add_argument("--gnu-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
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
    for reference in sorted(args.gnu_dir.rglob("*.o")):
        rel = reference.relative_to(args.gnu_dir)
        candidate = args.mini_dir / rel
        if not candidate.is_file():
            result = "MISSING"
            detail = "candidate_missing"
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
            except Exception as exc:
                result = "ORACLE_ERROR"
                detail = str(exc).replace("\t", " ").replace("\n", " ")
        counts[result] += 1
        rows.append((result, rel.as_posix(), detail))

    with args.out.open("w") as handle:
        handle.write("result\tobject\tdimensions\n")
        for result, rel, detail in rows:
            handle.write(f"{result}\t{rel}\t{detail}\n")
    print("MINIAS_AARCH64_OBJECT_ORACLE " + " ".join(
        f"{key.lower()}={counts[key]}"
        for key in ("EQUAL", "DIFF", "INCONCLUSIVE", "MISSING", "ORACLE_ERROR")
    ))
    for dimension, count in dimensions.most_common(12):
        print(f"MINIAS_AARCH64_OBJECT_DIMENSION count={count} name={dimension}")
    for result, rel, detail in rows:
        if rel.startswith(("arch/arm64/kernel/", "arch/arm64/mm/")) and result != "EQUAL":
            print(f"MINIAS_AARCH64_EARLY_OBJECT status={result} path={rel} dimensions={detail}")
    return 1 if counts["MISSING"] or counts["ORACLE_ERROR"] else 0


if __name__ == "__main__":
    sys.exit(main())
