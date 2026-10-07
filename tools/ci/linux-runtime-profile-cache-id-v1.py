#!/usr/bin/env python3
"""Compute a stable cache identity for the current Linux runtime MiniC profile.

The digest is intentionally computed from the clean checked-in source plus only
the scripts that participate in the current runtime profile.  It must be
computed before profile patches mutate src/, then reused verbatim for restore
and save operations.
"""
from __future__ import annotations

import hashlib
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROFILE = ROOT / "tools/ci/linux-runtime-build-minic-profile-v1.sh"

profile_text = PROFILE.read_text(encoding="utf-8")
patch_names = re.findall(r"^\s{2}(apply-[A-Za-z0-9_.-]+\.py)$", profile_text, flags=re.MULTILINE)
if not patch_names or len(patch_names) != len(set(patch_names)):
    raise SystemExit("runtime profile patch list is missing or contains duplicates")

files: set[Path] = set()
for directory in (ROOT / "src", ROOT / "include"):
    files.update(path for path in directory.rglob("*") if path.is_file())

for relative in (
    "Makefile",
    "tools/minic/main.c",
    "tools/minic-cc/main.c",
    "tools/ci/linux-runtime-build-minic-profile-v1.sh",
    "tools/ci/linux-runtime-profile-cache-id-v1.py",
    "tools/ci/scan-linux-nonalloc-explicit-sections-v0.py",
    "tests/external/linux/stage2_kbuild_cc.sh",
):
    path = ROOT / relative
    if not path.is_file():
        raise SystemExit(f"required profile input is missing: {relative}")
    files.add(path)

for name in patch_names:
    path = ROOT / "tools/ci" / name
    if not path.is_file():
        raise SystemExit(f"runtime profile patch is missing: {name}")
    files.add(path)

digest = hashlib.sha256()
digest.update(b"linux-runtime-profile-cache-id-v1\0")
for path in sorted(files, key=lambda p: p.relative_to(ROOT).as_posix()):
    relative = path.relative_to(ROOT).as_posix().encode("utf-8")
    data = path.read_bytes()
    digest.update(relative)
    digest.update(b"\0")
    digest.update(str(len(data)).encode("ascii"))
    digest.update(b"\0")
    digest.update(data)
    digest.update(b"\0")

print(digest.hexdigest())
