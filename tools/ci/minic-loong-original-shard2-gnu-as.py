#!/usr/bin/env python3
"""Replay eight SHA-bound, original GNU Linux 6.6.143 LoongArch C owners.

The input is one original failed-owner diagnostic shard artifact from official
seven-runner run #42345808682, not reconstructed or hand-written fixtures.
MiniC emits source assembly; GNU GCC -x assembler then emits ELF64 objects.
This is a focused C-owner compile/AS check, not a Linux link or boot claim.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import sys


def load_local(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def original_owner(path: str) -> PurePosixPath:
    owner = PurePosixPath(path)
    if owner.is_absolute() or ".." in owner.parts or owner.suffix != ".o":
        raise ValueError(f"unsafe original GNU owner: {path}")
    return owner


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact", type=Path, required=True)
    parser.add_argument("--shard", type=int, choices=range(7), default=2)
    parser.add_argument("--minic", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    ci = Path(__file__).resolve().parent
    frozen = load_local("original_loong_freeze", ci / "linux-three-isa-freeze-i-v1.py")
    candidate = load_local("original_loong_candidate", ci / "linux-three-isa-minic-candidate-v1.py")

    # The shared candidate producer invokes GNU AS with cwd=work/build.
    # Pass absolute staging paths: relative build/... targets would otherwise
    # be interpreted below work/build and every assembly would fail before
    # reading a byte of the genuine generated .s source.
    root = args.artifact.resolve() / "failed-owner-diagnostics"
    output_root = args.output.resolve()
    index_path = root / "index.json"
    index = json.loads(index_path.read_text())
    samples = index.get("samples")
    if (index.get("diagnostics_only") is not True or
            index.get("sample_count") != 8 or
            not isinstance(samples, list) or len(samples) != 8):
        raise ValueError("not the eight original GNU failure-owner samples")
    if output_root.exists():
        raise ValueError("refuse existing output (possible stale AS evidence)")
    work = output_root / "input"
    work.joinpath("frozen").mkdir(parents=True)
    work.joinpath("build").mkdir(parents=True)
    rows = []
    known = set()
    gcc = candidate.TARGETS["loongarch64"][2]

    for sample in samples:
        owner = original_owner(sample["owner"])
        if owner in known:
            raise ValueError(f"duplicated original GNU owner: {owner}")
        known.add(owner)
        directory = root / owner.with_suffix("")
        declared = sample["files"]
        paths = {
            "original.gnu.i": directory / "original.gnu.i",
            "original.gnu.o.cmd": directory / "original.gnu.o.cmd",
        }
        for filename, source in paths.items():
            entry = declared[filename]
            if (entry.get("status") != "COPIED" or
                    not re.fullmatch(r"[0-9a-f]{64}", entry.get("sha256", "")) or
                    source.is_symlink() or not source.is_file() or
                    source.stat().st_size != entry["bytes"] or
                    candidate.sha(source) != entry["sha256"]):
                raise ValueError(f"original GNU SHA/size mismatch {owner}: {filename}")

        # Reuse the same exact GNU Kbuild command/owner/source parser as the
        # seven-way frozen producer. It rejects mismatched .cmd owner, source,
        # output or ABI rather than inferring flags from unrelated input.
        _, mini_flags, as_flags, _ = frozen.gnu_argv(
            paths["original.gnu.o.cmd"].read_text(), gcc, str(owner),
            work / "frozen" / "never-run-original-gnu-i", "loongarch64")
        abi = [flag for flag in as_flags if flag.startswith("-mabi=")]
        if (abi != ["-mabi=lp64s"] or
                not frozen.valid_minic_flags(mini_flags, "loongarch64") or
                not frozen.valid_assembler_flags(as_flags)):
            raise ValueError(f"unsupported original GNU LoongArch ABI/flags: {owner}")

        relative = PurePosixPath("i") / owner.with_suffix(".i")
        frozen_copy = work / "frozen" / relative
        frozen_copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(paths["original.gnu.i"], frozen_copy)
        rows.append({
            "object": str(owner), "input": str(relative),
            "sha256": declared["original.gnu.i"]["sha256"],
            "cmd_sha256": declared["original.gnu.o.cmd"]["sha256"],
            "minic_flags": mini_flags, "gnu_as_flags": as_flags,
            "gnu_elf_abi": 1,  # LP64S, demanded by every original Kbuild .cmd.
        })

    compiler = args.minic.resolve(strict=True)
    data = {"target": "loongarch64", "gcc": gcc,
            "machine": candidate.TARGETS["loongarch64"][5]}
    output = output_root / "candidate"
    results = candidate.make_objects(rows, work, compiler, output,
                                     data, workers=2, timeout=300)
    (output_root / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    for result in results:
        print(f"ORIGINAL_LOONG_SHARD2_OWNER={result['object']} "
              f"status={result['status']} input_sha={result['input_sha256']} "
              f"object_sha={result.get('obj_sha256')} "
              f"detail={result.get('detail', 'none')}")
    passed = sum(row["status"] == "MINIC_S_GNU_AS_PASS" for row in results)
    if passed != 8:
        raise ValueError(f"original GNU shard2 MiniC/GNU AS {passed}/8")
    print(f"MINIC_LINUX_ORIGINAL_LOONG_SHARD{args.shard}_GNU_AS=PASS "
          "owners=8/8 original_GNU_i_cmd_SHA=16/16 "
          "MiniC_S=8/8 GNU_AS_ELF64_LP64S=8/8 "
          "GNU_LD_QEMU=NOT_RUN Linux_P1=NOT_CLAIMED")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError) as exc:
        sys.exit(f"MINIC_LINUX_ORIGINAL_LOONG_SHARD_GNU_AS=FAIL {exc}")
