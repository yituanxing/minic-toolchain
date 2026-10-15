#!/usr/bin/env python3
"""Verify exactly which Linux C object builds completed through MiniC.

Input successes must come from stage2_kbuild_cc.sh *after* both MiniC -S and
GNU assembly succeeded. This is stronger than merely observing that each .o
exists, which might silently certify stale or GCC-built objects as MiniC.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import re
from tempfile import TemporaryDirectory
from pathlib import Path, PurePosixPath
import sys


def clean_name(raw: str) -> str:
    name = raw.strip()
    p = PurePosixPath(name)
    if not name or p.is_absolute() or ".." in p.parts or len(p.parts) < 2 or not name.endswith(".o"):
        raise ValueError(f"invalid target in route evidence: {raw!r}")
    return name


def check(objects: list[str], successes: list[str]) -> tuple[int, int]:
    expected = list(map(clean_name, objects))
    observed = list(map(clean_name, successes))
    if not expected or len(expected) != len(set(expected)):
        raise ValueError("missing or duplicate target in reference list")
    counts = Counter(observed)
    missing = [x for x in expected if counts[x] == 0]
    duplicate = [x for x in expected if counts[x] != 1 and counts[x] != 0]
    if missing or duplicate:
        raise ValueError(
            f"MiniC route incomplete: expected={len(expected)} observed={len(observed)} "
            f"missing={len(missing)} {missing[:12]} duplicate={len(duplicate)} {duplicate[:12]}"
        )
    unexpected = set(counts) - set(expected)
    return len(expected), len(unexpected)



def check_final_object_hashes(
    objects: list[str], successes: list[str], attestations: list[str], candidates: Path
) -> tuple[int, int, int]:
    """Validate final object bytes against a post-MiniC/GNU-as digest.

    Kbuild may invoke the compiler for the same goal more than once even
    with -j1. Such duplicates are not evidence of GCC fallback: the hash
    record is emitted only by stage2_kbuild_cc.sh after successful MiniC
    compilation AND GNU assembly. Accept multiple successful invocations
    only when the actual final candidate is one of their recorded byte hashes.
    """
    expected = list(map(clean_name, objects))
    observed = list(map(clean_name, successes))
    if not expected or len(expected) != len(set(expected)):
        raise ValueError("missing or duplicate target in reference list")
    counts = Counter(observed)
    missing = [name for name in expected if counts[name] == 0]
    if missing:
        raise ValueError(f"missing authenticated MiniC routes: {missing[:12]}")
    by_target: dict[str, list[str]] = {}
    digest_counts = Counter()
    for line in attestations:
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        if not match:
            raise ValueError(f"malformed post-assembler hash evidence: {line[:120]!r}")
        name = clean_name(match.group(2))
        by_target.setdefault(name, []).append(match.group(1))
        digest_counts[name] += 1
    if digest_counts != counts:
        raise ValueError("compiler success and digest trace disagree")
    for name in expected:
        target = candidates / name
        if target.is_symlink() or not target.is_file():
            raise ValueError(f"candidate object missing or symlink: {name}")
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        if digest not in by_target[name]:
            raise ValueError(f"final candidate differs from MiniC-produced bytes: {name}")
    duplicates = sum(counts[name] - 1 for name in expected)
    return len(expected), len(set(counts) - set(expected)), duplicates


def load_names(path: Path) -> list[str]:
    return [l.strip() for l in path.read_text(encoding="utf-8").splitlines()
            if l.strip() and not l.lstrip().startswith("#")]


def self_test() -> None:
    a, b = ["kernel/foo.o", "mm/bar.o"], ["mm/bar.o", "kernel/foo.o"]
    assert check(a, b) == (2, 0)
    assert check(a, b + ["drivers/side.o"]) == (2, 1)
    for observed in (b[:1], b + ["kernel/foo.o"], ["kernel/foo.o", "../escape.o"]):
        try:
            check(a, observed)
        except ValueError:
            continue
        raise AssertionError("route audit accepted missing, duplicate or unsafe evidence")
    with TemporaryDirectory() as t:
        base = Path(t)
        for name, content in (("kernel/foo.o", b"mini-f"), ("mm/bar.o", b"mini-b")):
            p = base / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(content)
        obs = b + ["kernel/foo.o", "drivers/side.o"]
        ds = [hashlib.sha256((base / n).read_bytes()).hexdigest() + "  " + n
              for n in b + ["kernel/foo.o"]]
        # The unrelated auxiliary still needs a matching successful digest.
        ds.append(hashlib.sha256(b"aux").hexdigest() + "  drivers/side.o")
        assert check_final_object_hashes(a, obs, ds, base) == (2, 1, 1)
        # A GNU overwrite or altered candidate must never be accepted.
        (base / "kernel/foo.o").write_bytes(b"gcc-replacement")
        try:
            check_final_object_hashes(a, obs, ds, base)
        except ValueError:
            pass
        else:
            raise AssertionError("accepted object not emitted by MiniC")
        (base / "kernel/foo.o").write_bytes(b"mini-f")
        for bad in (ds[:-1], ds + [ds[0]], ds[:-2] + ["bad hash"]):
            try:
                check_final_object_hashes(a, obs, bad, base)
            except ValueError:
                pass
            else:
                raise AssertionError("accepted incomplete/invalid hash evidence")
    print("MINIC_ROUTE_AUDIT_SELFTEST=PASS cases=10")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--objects-file", type=Path)
    parser.add_argument("--success-trace", type=Path)
    parser.add_argument("--object-digest-trace", type=Path)
    parser.add_argument("--candidates", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.self_test:
            self_test()
            return 0
        if args.objects_file is None or args.success_trace is None:
            parser.error("--objects-file and --success-trace are required")
        if args.object_digest_trace is not None or args.candidates is not None:
            if args.object_digest_trace is None or args.candidates is None:
                parser.error("--object-digest-trace requires --candidates and vice versa")
            count, extra, duplicates = check_final_object_hashes(
                load_names(args.objects_file), load_names(args.success_trace),
                load_names(args.object_digest_trace), args.candidates)
            print(f"MINIC_ROUTE_AUDIT=PASS minic_compiled={count} "
                  f"ancillary_minic_compiled={extra} authenticated_rebuilds={duplicates}")
        else:
            count, extra = check(load_names(args.objects_file), load_names(args.success_trace))
            print(f"MINIC_ROUTE_AUDIT=PASS minic_compiled={count} ancillary_minic_compiled={extra}")
    except (ValueError, OSError, AssertionError) as exc:
        print(f"MINIC_ROUTE_AUDIT=FAIL reason={exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
