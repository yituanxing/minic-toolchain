#!/usr/bin/env python3
"""Fail-closed handling of Kbuild rebuilds during GNU/MiniC object overlay.

When a changed vDSO offset header causes Kbuild to rebuild a MiniC-selected
.o using GNU GCC, the resulting Image must NOT be tested as all-MiniC.
A single guarded repair is safe only when generated header *content* remains
byte-identical and each overwritten TU appears as a real CC in link.log.
The caller then reruns GNU linking and independently re-verifies ALL objects.
If generated header content changes, stop: the affected TUs need fresh GNU
preprocessing and MiniC recompilation with the changed header.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import sys

SCHEMA = "minic-gnu-kbuild-regeneration-guard-v1"
CC = re.compile(r"^\s+CC\s+(\S+\.o)\s*$", re.M)
EXCLUDE_HEADERS = {"include/generated/utsversion.h"}  # link-generated metadata


class GuardError(Exception):
    pass


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fp:
        for b in iter(lambda: fp.read(1024 * 1024), b""):
            h.update(b)
    return h.hexdigest()


def generated(out: Path) -> dict[str, str]:
    result = {}
    for root in (out / "include/generated", out / "arch/riscv/include/generated"):
        if not root.exists():
            continue
        for p in root.rglob("*"):
            if p.is_dir() and not p.is_symlink():
                continue
            if not p.is_file() or p.is_symlink():
                raise GuardError(f"generated input absent or unsafe: {p}")
            rel = p.relative_to(out).as_posix()
            if rel in EXCLUDE_HEADERS:
                continue
            result[rel] = sha(p)
    if not result:
        raise GuardError("no generated-header evidence")
    return dict(sorted(result.items()))


def object_paths(file: Path) -> list[str]:
    objects = []
    seen = set()
    for item in file.read_text().splitlines():
        item = item.strip()
        if not item or item.startswith("#"):
            continue
        p = PurePosixPath(item)
        if p.is_absolute() or ".." in p.parts or len(p.parts) < 2 or p.suffix != ".o" or item in seen:
            raise GuardError(f"unsafe or duplicate object path: {item!r}")
        seen.add(item)
        objects.append(item)
    return objects


def snapshot(out: Path, dest: Path) -> None:
    obj = {"schema": SCHEMA, "headers": generated(out)}
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n")


def selected_generated_header_dependents(
    golden_out: Path, names: list[str], changed: list[str]
) -> list[str]:
    """Inspect pinned GNU Kbuild .cmd dependency closures, fail closed.

    A generated-header change is relevant to MiniC source only when the
    header is actually a recorded compile dependency of a selected C TU.
    Unknown/missing .cmd dependency metadata is never silently accepted.
    """
    affected = []
    for name in names:
        p = PurePosixPath(name)
        depfile = golden_out / p.parent / ("." + p.name + ".cmd")
        if not depfile.is_file() or depfile.is_symlink():
            raise GuardError(f"missing pinned Kbuild dependency closure: {name}")
        body = depfile.read_text(encoding="utf-8", errors="replace")
        if not re.search(r"(?m)^deps_[^\n]+\s*:=", body):
            raise GuardError(f"no pinned Kbuild dependency closure: {name}")
        if any(re.search(r"(?<![A-Za-z0-9_./-])" + re.escape(header) +
                         r"(?![A-Za-z0-9_./-])", body) for header in changed):
            affected.append(name)
    return affected


def repair(out: Path, minic: Path, selected: Path, log: Path, evidence: Path,
           golden_out: Path | None = None) -> list[str]:
    baseline = json.loads(evidence.read_text())
    if baseline.get("schema") != SCHEMA:
        raise GuardError("invalid generated-header baseline schema")
    after = generated(out)
    selected_names = object_paths(selected)
    if after != baseline.get("headers"):
        changed = sorted(k for k in set(after) | set(baseline.get("headers", {}))
                         if after.get(k) != baseline.get("headers", {}).get(k))
        if golden_out is None:
            raise GuardError("generated header content changed, MUST re-preprocess without pinned dependency evidence: "
                             + ",".join(changed[:25]))
        affected = selected_generated_header_dependents(golden_out, selected_names, changed)
        if affected:
            raise GuardError("generated header content changed; only affected MiniC TUs must "
                             "re-preprocess: headers=" + ",".join(changed[:25])
                             + " affected=" + ",".join(affected[:40])
                             + f" affected_count={len(affected)}")
        print("COHORT_KBUILD_HEADER_CHANGE=SAFE_NONDEPENDENT "
              f"changed={','.join(changed)} mini_tus={len(selected_names)}")
    
    log_text = log.read_text()
    recompiled = set(CC.findall(log_text))
    # Never repair an .o just because it has the wrong digest. Prove Kbuild
    # actually replaced this selected TU with its GCC compile step.
    bad = []
    for name in selected_names:
        path, candidate = out / name, minic / name
        if not path.is_file() or not candidate.is_file():
            raise GuardError(f"missing selected candidate/output: {name}")
        if sha(path) != sha(candidate):
            bad.append(name)
    unknown = sorted(set(bad) - recompiled)
    if unknown:
        raise GuardError("MiniC candidate mismatch without Kbuild CC proof: " + ",".join(unknown[:25]))
    # GCC recompiling an UNSELECTED owner is not repaired here; caller's
    # independent GCC SHA256 audit still rejects it. No exceptions added.
    for name in bad:
        path = out / name
        shutil.copy2(minic / name, path)
        os.utime(path, None)
    return bad


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--mode", choices=("snapshot", "repair"), required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--evidence", type=Path, required=True)
    p.add_argument("--minic", type=Path)
    p.add_argument("--selected", type=Path)
    p.add_argument("--link-log", type=Path)
    p.add_argument("--golden-out", type=Path,
                   help="immutable GNU Kbuild .cmd dependency reference")
    args = p.parse_args()
    try:
        if args.mode == "snapshot":
            snapshot(args.out, args.evidence)
            print(f"COHORT_KBUILD_HEADERS=SNAPSHOT entries={len(generated(args.out))}")
        else:
            if not all((args.minic, args.selected, args.link_log)):
                raise GuardError("repair requires --minic, --selected, --link-log")
            fixed = repair(args.out, args.minic, args.selected, args.link_log,
                           args.evidence, args.golden_out)
            print(f"COHORT_KBUILD_REPAIR={'APPLIED' if fixed else 'NOT_NEEDED'} count={len(fixed)} objects={','.join(fixed)}")
    except (GuardError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"COHORT_KBUILD_REPAIR=INCONCLUSIVE reason={exc}", file=sys.stderr)
        return 8
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
