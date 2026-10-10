#!/usr/bin/env python3
"""Deterministic seven-runner MiniC object partition and fail-closed reassembly.

Shards never certify runtime. Only the full, provenance-checked union may be
fed to the existing GNU golden relink / QEMU verifier.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import sys


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def objects(path: Path) -> list[str]:
    names = [x.strip() for x in path.read_text().splitlines()
             if x.strip() and not x.lstrip().startswith("#")]
    if not names or len(names) != len(set(names)):
        raise ValueError(f"empty or duplicate object list: {path}")
    for name in names:
        p = PurePosixPath(name)
        if (p.is_absolute() or ".." in p.parts or len(p.parts) < 2
                or not name.endswith(".o") or "\\" in name):
            raise ValueError(f"unsafe object path: {name}")
    return names


def members(all_objects: list[str], shard: int, count: int) -> list[str]:
    if count != 7 or not 0 <= shard < count:
        raise ValueError("this producer requires exactly seven fixed shards")
    return all_objects[shard::count]


def contract(*parts: str) -> str:
    return hashlib.sha256(("\n".join(parts) + "\n").encode()).hexdigest()


def check_elf(ref: Path, candidate: Path, name: str) -> None:
    for path in (ref, candidate):
        with path.open("rb") as file:
            h = file.read(52)
        if (len(h) != 52 or h[:6] != b"\x7fELF\x02\x01"
                or struct.unpack_from("<H", h, 16)[0] != 1
                or struct.unpack_from("<H", h, 18)[0] != 243):
            raise ValueError(f"not ELF64 LE RISC-V ET_REL: {name}: {path}")
        if path == ref:
            flags = h[48:52]
        elif h[48:52] != flags:
            raise ValueError(f"RISC-V ABI flags mismatch: {name}")


def split(args: argparse.Namespace) -> None:
    names = objects(args.full)
    if not 1200 <= len(names) <= 3000:
        raise ValueError(f"not a plausible certified full C universe: {len(names)}")
    shard_objects = members(names, args.shard, args.count)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("# MiniC GNU-golden shard; not standalone runtime evidence\n"
                           + "\n".join(shard_objects) + "\n")
    print(f"GNU_MINIC_SHARD_PLAN=PASS shard={args.shard}/{args.count} "
          f"selected={len(shard_objects)} full={len(names)}")


def merge(args: argparse.Namespace) -> None:
    names = objects(args.full)
    if not 1200 <= len(names) <= 3000:
        raise ValueError(f"not a plausible certified full C universe: {len(names)}")
    cfg_expected = next((s.partition("=")[2] for s in args.provenance.joinpath(
        "gcc-baseline.txt").read_text().splitlines() if s.startswith("config_sha256=")), "")
    if cfg_expected != sha(args.out / ".config"):
        raise ValueError("certified GNU config identity mismatch")
    golden = {}
    for line in (args.provenance / "gcc-objects.sha256").read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            raise ValueError("bad GCC reference object manifest")
        golden[match.group(2)] = match.group(1)
    compiler = sha(args.minic)
    assembler = sha(args.assembler)
    gnu_image = sha(args.out / "arch/riscv/boot/Image")
    gold_manifest = sha(args.provenance / "gcc-objects.sha256")
    collected: dict[str, Path] = {}
    for index in range(7):
        shard_dir = args.shards / f"shard-{index}"
        subset = members(names, index, 7)
        subset_file = shard_dir / "full-objects.txt"
        if objects(subset_file) != subset:
            raise ValueError(f"shard {index} has noncanonical target membership")
        identity = contract(cfg_expected, gnu_image, compiler, assembler,
                            gold_manifest, sha(subset_file))
        if (shard_dir / "minic-identity.txt").read_text().strip() != identity:
            raise ValueError(f"MiniC/GNU/cache identity mismatch in shard {index}")
        log = (shard_dir / "cohort.log").read_text()
        n = len(subset)
        if (f"COHORT_ABI=PASS objects={n}" not in log
                or f"COHORT_PRODUCE_ONLY=PASS objects={n}" not in log
                or not (f"COHORT_COMPILE=PASS objects={n}" in log
                        or f"COHORT_CANDIDATE_CACHE=HIT objects={n}" in log)):
            raise ValueError(f"shard {index} missing complete MiniC route/ABI evidence")
        manifest_lines = (shard_dir / "minic-manifest.sha256").read_text().splitlines()
        if len(manifest_lines) != len(subset):
            raise ValueError(f"partial candidate manifest in shard {index}")
        actual_files = {p.relative_to(shard_dir / "minic").as_posix()
                        for p in (shard_dir / "minic").rglob("*") if p.is_file()}
        if actual_files != set(subset):
            raise ValueError(f"extra or missing candidate objects in shard {index}")
        for name, line in zip(subset, manifest_lines):
            match = re.fullmatch(r"([0-9a-f]{64})  minic/(.+)", line)
            if not match or match.group(2) != name:
                raise ValueError(f"unexpected candidate manifest entry: {name}")
            candidate = shard_dir / "minic" / name
            if candidate.is_symlink() or not candidate.is_file() or sha(candidate) != match.group(1):
                raise ValueError(f"candidate object hash mismatch: {name}")
            reference = args.out / name
            if golden.get(name) != sha(reference):
                raise ValueError(f"GCC golden object changed: {name}")
            check_elf(reference, candidate, name)
            if name in collected:
                raise ValueError(f"duplicate object from shards: {name}")
            collected[name] = candidate
        print(f"GNU_MINIC_SHARD_VERIFIED shard={index} count={n}")
    if set(collected) != set(names):
        raise ValueError("shard union is not exactly the full C universe")
    if (args.evidence / "minic").exists() or (args.evidence / "minic-identity.txt").exists():
        raise ValueError("refuse to merge over pre-existing candidate data")
    args.evidence.mkdir(parents=True, exist_ok=True)
    target_dir = args.evidence / "minic"
    target_dir.mkdir()
    manifests = []
    for name in names:
        dest = target_dir / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(collected[name], dest)
        manifests.append(f"{sha(dest)}  minic/{name}")
    (args.evidence / "minic-manifest.sha256").write_text("\n".join(manifests) + "\n")
    (args.evidence / "minic-identity.txt").write_text(
        contract(cfg_expected, gnu_image, compiler, assembler, gold_manifest, sha(args.full)) + "\n")
    print(f"GNU_MINIC_SHARD_MERGE=PASS objects={len(names)} shards=7")


def self_test() -> None:
    sample = [f"lib/o{n}.o" for n in range(2064)]
    parts = [members(sample, i, 7) for i in range(7)]
    assert sum(map(len, parts)) == 2064
    assert set.union(*(set(p) for p in parts)) == set(sample)
    assert len(set.intersection(*(set(p) for p in parts))) == 0
    for bad in ("../bad.o", "/root/file.o", "foo.o", "a/../foo.o"):
        from tempfile import TemporaryDirectory
        with TemporaryDirectory() as t:
            f = Path(t) / "list.txt"
            f.write_text(bad + "\n")
            try:
                objects(f)
            except ValueError:
                pass
            else:
                raise AssertionError(f"accepted unsafe target {bad}")
    try:
        members(sample, 7, 7)
    except ValueError:
        pass
    else:
        raise AssertionError("accepted invalid shard index")
    print("GNU_MINIC_SHARD_SELFTEST=PASS partition=2064 members=7 unsafe=4")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    split_p = sub.add_parser("split")
    split_p.add_argument("--full", type=Path, required=True)
    split_p.add_argument("--shard", type=int, required=True)
    split_p.add_argument("--count", type=int, default=7)
    split_p.add_argument("--output", type=Path, required=True)
    merge_p = sub.add_parser("merge")
    for name in ("full", "shards", "evidence", "provenance", "out", "minic", "assembler"):
        merge_p.add_argument("--" + name, type=Path, required=True)
    sub.add_parser("self-test")
    a = p.parse_args()
    try:
        if a.command == "split":
            split(a)
        elif a.command == "merge":
            merge(a)
        else:
            self_test()
        return 0
    except (OSError, ValueError, AssertionError) as exc:
        print(f"GNU_MINIC_SHARD=FAIL {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
