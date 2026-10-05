#!/usr/bin/env python3
"""Prepare a deterministic Linux object-set experiment.

This tool deliberately does not link or run QEMU.  It restores every object in
an ordered experiment universe from a known-good GCC baseline, overlays the
selected MiniC candidates, verifies ELF/provenance, and writes a machine-readable
manifest.  Existing relink/QEMU/classifier tools consume the resulting work tree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import tempfile
from typing import Iterable

SCHEMA = "minic-linux-runtime-object-set-v1"


class ObjectSetError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def normalize_object_path(raw: str) -> str:
    value = raw.strip().replace("\\", "/")
    if not value:
        raise ObjectSetError("empty object path")
    p = PurePosixPath(value)
    if p.is_absolute() or value.startswith("/"):
        raise ObjectSetError(f"absolute object path is forbidden: {raw!r}")
    if any(part in ("", ".", "..") for part in p.parts):
        raise ObjectSetError(f"unsafe object path: {raw!r}")
    normalized = p.as_posix()
    if not normalized.endswith(".o"):
        raise ObjectSetError(f"object path must end in .o: {raw!r}")
    return normalized


def read_object_list(path: Path) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        obj = normalize_object_path(line)
        if obj in seen:
            raise ObjectSetError(f"duplicate object at {path}:{lineno}: {obj}")
        seen.add(obj)
        result.append(obj)
    if not result:
        raise ObjectSetError(f"object list is empty: {path}")
    return result


def ensure_within(root: Path, path: Path, *, label: str) -> Path:
    root_real = root.resolve()
    path_real = path.resolve(strict=False)
    try:
        path_real.relative_to(root_real)
    except ValueError as exc:
        raise ObjectSetError(f"{label} escapes root: {path}") from exc
    return path_real


def checked_object(root: Path, rel: str, *, label: str) -> Path:
    path = root / rel
    ensure_within(root, path, label=label)
    if not path.is_file():
        raise ObjectSetError(f"missing {label}: {rel}")
    if path.is_symlink():
        raise ObjectSetError(f"symlink {label} is forbidden: {rel}")
    assert_elf_rel(path, label=f"{label} {rel}")
    return path


def assert_elf_rel(path: Path, *, label: str) -> None:
    with path.open("rb") as f:
        header = f.read(20)
    if len(header) < 18 or header[:4] != b"\x7fELF":
        raise ObjectSetError(f"not an ELF object: {label}")
    elf_class = header[4]
    elf_data = header[5]
    if elf_class not in (1, 2):
        raise ObjectSetError(f"unsupported ELF class in {label}: {elf_class}")
    if elf_data == 1:
        e_type = struct.unpack_from("<H", header, 16)[0]
    elif elf_data == 2:
        e_type = struct.unpack_from(">H", header, 16)[0]
    else:
        raise ObjectSetError(f"unsupported ELF data encoding in {label}: {elf_data}")
    if e_type != 1:
        raise ObjectSetError(f"ELF is not ET_REL in {label}: e_type={e_type}")


def parse_explicit(values: Iterable[str]) -> list[str]:
    result: list[str] = []
    seen: set[str] = set()
    for raw in values:
        obj = normalize_object_path(raw)
        if obj in seen:
            raise ObjectSetError(f"duplicate explicit object: {obj}")
        seen.add(obj)
        result.append(obj)
    return result


def choose_objects(universe: list[str], args: argparse.Namespace) -> list[str]:
    universe_set = set(universe)
    explicit = parse_explicit(args.object)
    if args.mode == "baseline":
        if explicit or args.prefix is not None:
            raise ObjectSetError("baseline mode accepts neither --object nor --prefix")
        return []
    if args.mode == "all":
        if explicit or args.prefix is not None:
            raise ObjectSetError("all mode accepts neither --object nor --prefix")
        return list(universe)
    if args.mode == "prefix":
        if args.prefix is None:
            raise ObjectSetError("prefix mode requires --prefix N")
        if explicit:
            raise ObjectSetError("prefix mode does not accept --object")
        if args.prefix < 0 or args.prefix > len(universe):
            raise ObjectSetError(
                f"prefix out of range: {args.prefix}; universe has {len(universe)} objects"
            )
        return universe[: args.prefix]
    if args.mode in ("subset", "single", "all-except"):
        if args.prefix is not None:
            raise ObjectSetError(f"{args.mode} mode does not accept --prefix")
        if args.mode == "single" and len(explicit) != 1:
            raise ObjectSetError("single mode requires exactly one --object")
        if args.mode in ("subset", "all-except") and not explicit:
            raise ObjectSetError(f"{args.mode} mode requires at least one --object")
        unknown = [obj for obj in explicit if obj not in universe_set]
        if unknown:
            raise ObjectSetError("object is outside ordered universe: " + ", ".join(unknown))
        if args.mode == "all-except":
            excluded = set(explicit)
            return [obj for obj in universe if obj not in excluded]
        selected = set(explicit)
        return [obj for obj in universe if obj in selected]
    raise AssertionError(args.mode)


def copy_atomic(src: Path, dst: Path, *, work_root: Path) -> None:
    ensure_within(work_root, dst, label="work destination")
    dst.parent.mkdir(parents=True, exist_ok=True)
    ensure_within(work_root, dst.parent, label="work parent")
    if dst.is_symlink():
        raise ObjectSetError(f"refusing to overwrite symlink destination: {dst}")
    fd, temp_name = tempfile.mkstemp(prefix=f".{dst.name}.", dir=str(dst.parent))
    os.close(fd)
    temp = Path(temp_name)
    try:
        shutil.copy2(src, temp)
        os.replace(temp, dst)
    finally:
        temp.unlink(missing_ok=True)


def prepare(args: argparse.Namespace) -> dict:
    baseline = args.baseline_root.resolve()
    candidate = args.candidate_root.resolve()
    work = args.work_root.resolve()
    if not baseline.is_dir():
        raise ObjectSetError(f"baseline root is not a directory: {baseline}")
    if not candidate.is_dir():
        raise ObjectSetError(f"candidate root is not a directory: {candidate}")
    if work.exists() and not work.is_dir():
        raise ObjectSetError(f"work root is not a directory: {work}")

    universe = read_object_list(args.objects_file)
    selected = choose_objects(universe, args)
    selected_set = set(selected)

    entries = []
    baseline_paths: dict[str, Path] = {}
    candidate_paths: dict[str, Path] = {}

    # Validate the complete reset set before mutating anything.
    for rel in universe:
        baseline_paths[rel] = checked_object(baseline, rel, label="baseline object")
    for rel in selected:
        candidate_paths[rel] = checked_object(candidate, rel, label="candidate object")

    # No filesystem mutation occurs before the complete input set validates.
    if not args.dry_run:
        work.mkdir(parents=True, exist_ok=True)

    for index, rel in enumerate(universe, 1):
        base = baseline_paths[rel]
        cand = candidate_paths.get(rel)
        baseline_sha = sha256_file(base)
        candidate_sha = sha256_file(cand) if cand else None
        final_source = cand if cand else base
        expected_sha = candidate_sha if cand else baseline_sha
        dst = work / rel
        if not args.dry_run:
            # Critical stale-state barrier: reset every universe object to GCC first.
            copy_atomic(base, dst, work_root=work)
            if cand:
                copy_atomic(cand, dst, work_root=work)
            assert_elf_rel(dst, label=f"prepared object {rel}")
            result_sha = sha256_file(dst)
            if result_sha != expected_sha:
                raise ObjectSetError(
                    f"provenance mismatch after preparing {rel}: "
                    f"expected {expected_sha}, got {result_sha}"
                )
        else:
            result_sha = expected_sha
        entries.append(
            {
                "index": index,
                "path": rel,
                "provenance": "minic" if rel in selected_set else "gcc-baseline",
                "baseline_sha256": baseline_sha,
                "candidate_sha256": candidate_sha,
                "result_sha256": result_sha,
                "source": str(final_source),
            }
        )

    manifest = {
        "schema": SCHEMA,
        "mode": args.mode,
        "dry_run": bool(args.dry_run),
        "baseline_root": str(baseline),
        "candidate_root": str(candidate),
        "work_root": str(work),
        "objects_file": str(args.objects_file.resolve()),
        "universe_count": len(universe),
        "selected_count": len(selected),
        "selected_objects": selected,
        "objects": entries,
    }
    if args.manifest:
        if args.dry_run:
            # Deliberately no filesystem writes in dry-run mode.
            pass
        else:
            args.manifest.parent.mkdir(parents=True, exist_ok=True)
            args.manifest.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
    return manifest


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--baseline-root", type=Path, required=True)
    p.add_argument("--candidate-root", type=Path, required=True)
    p.add_argument("--work-root", type=Path, required=True)
    p.add_argument("--objects-file", type=Path, required=True)
    p.add_argument(
        "--mode",
        choices=("baseline", "all", "prefix", "subset", "single", "all-except"),
        required=True,
    )
    p.add_argument("--prefix", type=int)
    p.add_argument("--object", action="append", default=[])
    p.add_argument("--manifest", type=Path)
    p.add_argument("--dry-run", action="store_true")
    return p


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        manifest = prepare(args)
    except (ObjectSetError, OSError) as exc:
        print(f"OBJECT_SET_ERROR={exc}", file=sys.stderr)
        return 2
    print(
        f"OBJECT_SET=PASS mode={manifest['mode']} "
        f"selected={manifest['selected_count']} universe={manifest['universe_count']}"
    )
    if args.manifest and not args.dry_run:
        print(f"OBJECT_SET_MANIFEST={args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
