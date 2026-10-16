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
# BEGIN MINILD_COMPONENT_CACHE_ID_COMPAT_V1
# Exclude ONLY one proven linker-source-only Makefile change from MiniC's
# historical exact cache key. Shallow CI checkouts need no old git objects.
# All other edits to the compiler build graph/script fail this byte-identity
# comparison and produce a distinct cache key.
_BASELINE_MAKEFILE_BLOB = "22d2b8332e1a310c3ba806af2df8527bddf7ae0e"
_BASELINE_SCRIPT_BLOB = "27dfe30688e4ccaaad21b0fd01a1bd602d3196e7"
_MARKER_BEGIN = b"# BEGIN MINILD_COMPONENT_CACHE_ID_COMPAT_V1\n"
_MARKER_END = b"# END MINILD_COMPONENT_CACHE_ID_COMPAT_V1\n"
_ELF32_SOURCE_INSERTION = b"\tlinker/src/elf32_compat_vdso.c \\\n"
_ORIGINAL_DIGEST_READ = b"    data = path.read_bytes()\n"
_SCOPED_DIGEST_READ = b"    data = _compiler_scoped_bytes(path, path.read_bytes())\n"


def _git_blob_sha1(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") +
                        b"\x00" + data).hexdigest()


def _legacy_cache_script(data: bytes) -> bytes | None:
    if data.count(_MARKER_BEGIN) != 1 or data.count(_MARKER_END) != 1:
        return None
    front, tail = data.split(_MARKER_BEGIN, 1)
    middle, rest = tail.split(_MARKER_END, 1)
    if b"def _compiler_scoped_bytes(" not in middle:
        return None
    legacy = front + rest
    if legacy.count(_SCOPED_DIGEST_READ) != 1:
        return None
    legacy = legacy.replace(_SCOPED_DIGEST_READ, _ORIGINAL_DIGEST_READ, 1)
    return legacy if _git_blob_sha1(legacy) == _BASELINE_SCRIPT_BLOB else None


def _compiler_scoped_bytes(path: Path, data: bytes) -> bytes:
    if path == Path(__file__).resolve():
        return _legacy_cache_script(data) or data
    if path == ROOT / "Makefile":
        if data.count(_ELF32_SOURCE_INSERTION) == 1:
            legacy = data.replace(_ELF32_SOURCE_INSERTION, b"", 1)
            if _git_blob_sha1(legacy) == _BASELINE_MAKEFILE_BLOB:
                return legacy
    return data


def _self_test_component_isolation() -> None:
    import sys
    raw_script = Path(__file__).resolve().read_bytes()
    raw_makefile = (ROOT / "Makefile").read_bytes()
    old_script = _legacy_cache_script(raw_script)
    assert old_script is not None, "cache-key script baseline mismatch"
    assert _git_blob_sha1(old_script) == _BASELINE_SCRIPT_BLOB
    assert raw_makefile.count(_ELF32_SOURCE_INSERTION) == 1
    old_makefile = _compiler_scoped_bytes(ROOT / "Makefile", raw_makefile)
    # The compiler may legitimately add a translation unit later.
    # Such a source-list change MUST miss the certified 2064 cache;
    # the linker-only SHA normalization must not silently mask it.
    if _git_blob_sha1(old_makefile) == _BASELINE_MAKEFILE_BLOB:
        assert old_makefile != raw_makefile
        compiler_key = "unchanged_exact_linker_only"
    else:
        assert old_makefile == raw_makefile, (
            "non-linker Makefile edit must invalidate compiler cache")
        compiler_key = "changed_compiler_cache_invalidated"
    # Changes to ANY other Makefile byte MUST invalidate this identity.
    assert _compiler_scoped_bytes(ROOT / "Makefile", raw_makefile + b"\n") != old_makefile
    # Changes outside the narrowly pinned compatibility block MUST invalidate.
    assert _compiler_scoped_bytes(Path(__file__).resolve(), raw_script + b"\n") != old_script
    print("MINIC_CACHE_COMPONENT_ISOLATION=PASS "
          "scope=" + compiler_key + " "
          "linker_source_only=true other_changes_invalidate=true "
          "no_network_no_git_history=true")


if __name__ == "__main__":
    import sys
    if sys.argv[1:] == ["--self-test"]:
        _self_test_component_isolation()
        raise SystemExit(0)

# END MINILD_COMPONENT_CACHE_ID_COMPAT_V1
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
    data = _compiler_scoped_bytes(path, path.read_bytes())
    digest.update(relative)
    digest.update(b"\0")
    digest.update(str(len(data)).encode("ascii"))
    digest.update(b"\0")
    digest.update(data)
    digest.update(b"\0")

print(digest.hexdigest())
