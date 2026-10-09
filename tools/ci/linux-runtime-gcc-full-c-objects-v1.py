#!/usr/bin/env python3
"""Derive ALL GCC-produced C TU objects from an exact, QEMU-certified Kbuild tree.

The GNU golden manifest is the object universe; Kbuild's *.o.cmd identifies
whether GCC compiled a C source. Never assume each ELF .o maps to a same-name
.c (Linux builds composite/renamed objects and .S-only assembly objects).
This script will fail closed on incomplete/implausible source metadata.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import re
import sys

SOURCE = re.compile(r"^source_[^ \n]*\s*:=\s*(\S+\.c)\s*$", re.M)
SAVED = re.compile(r"^(?:savedcmd|cmd)_[^\n]*\s*:=\s*([^\n]+)", re.M)
SRC_TOKEN = re.compile(r"(?<!\S)(?:\S+[/])?[\w.+-]+\.c(?!\S)")
LINK_TEMP = re.compile(r"(?:\.tmp_vmlinux\.kallsyms[1-3]\.o|init/version-timestamp\.o)")

def parse_entries(manifest: Path):
    for line in manifest.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if len(fields) != 2 or not re.fullmatch(r"[0-9a-f]{64}", fields[0]):
            raise ValueError(f"invalid SHA256 manifest record: {line[:100]}")
        item = fields[1]
        path = PurePosixPath(item)
        if path.is_absolute() or ".." in path.parts or not item.endswith(".o"):
            raise ValueError(f"invalid golden object path: {item}")
        yield fields[0], item

def examine(out: Path, golden: Path):
    selected = []
    exclusions = []
    for digest, item in parse_entries(golden):
        obj = out / item
        if not obj.is_file():
            raise ValueError(f"missing certified GNU object: {item}")
        # The golden object data must match the archive/provenance manifest.
        h = hashlib.sha256(obj.read_bytes()).hexdigest()
        if h != digest:
            raise ValueError(f"golden object hash mismatch: {item}")
        p = PurePosixPath(item)
        cmd = out / p.parent / ("." + p.name + ".cmd")
        if LINK_TEMP.fullmatch(item) or item.startswith(".tmp_vmlinux."):
            exclusions.append((item, "link-generated"))
            continue
        if not cmd.is_file():
            exclusions.append((item, "no-kbuild-command"))
            continue
        cmd_text = cmd.read_text(encoding="utf-8", errors="replace")
        match = SOURCE.search(cmd_text)
        if match:
            source = match.group(1)
        else:
            saved = SAVED.search(cmd_text)
            if not saved:
                exclusions.append((item, "no-c-source-metadata"))
                continue
            command = saved.group(1)
            candidates = SRC_TOKEN.findall(command)
            if len(candidates) != 1:
                exclusions.append((item, "non-c-or-ambiguous-command"))
                continue
            source = candidates[0]
        if not source.endswith(".c"):
            exclusions.append((item, "not-c"))
            continue
        # The .cmd must have compiled a C file, not assembler or objcopy.
        # Kbuild source_ metadata is the authoritative mapping.
        selected.append(item)
    return selected, exclusions

def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--manifest", type=Path, required=True)
    p.add_argument("--prefix", type=Path, help="previously certified prefix; optional")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--excluded", type=Path, required=True)
    p.add_argument("--min-c-objects", type=int, default=1200)
    args = p.parse_args()
    selected, excluded = examine(args.out, args.manifest)
    if len(selected) < args.min_c_objects:
        raise ValueError(
            f"only {len(selected)} GCC-produced C TUs found; "
            "do not infer a full kernel from an incomplete manifest")
    prior = []
    if args.prefix:
        prior = [s.strip() for s in args.prefix.read_text().splitlines()
                 if s.strip() and not s.strip().startswith("#")]
        unrecognized = set(prior) - set(selected)
        if unrecognized:
            raise ValueError("old prefix contains objects outside exact GCC C universe: "
                             + repr(sorted(unrecognized)[:15]))
    ordered = prior + sorted(set(selected) - set(prior))
    if len(ordered) != len(set(ordered)):
        raise ValueError("duplicate full object name")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.excluded.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("# Exact full GNU 6.6.143 GCC-C TU build universe\n" +
                           "\n".join(ordered) + "\n", encoding="utf-8")
    args.excluded.write_text("object\treason\n" +
                             "".join(f"{obj}\t{reason}\n" for obj,reason in excluded),
                             encoding="utf-8")
    print(f"GNU_FULL_C_OBJECTS=PASS selected={len(selected)} excluded={len(excluded)} "
          f"prior_certified_prefix={len(prior)} out={args.output}")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as e:
        print(f"GNU_FULL_C_OBJECTS=FAIL reason={e}", file=sys.stderr)
        sys.exit(2)
