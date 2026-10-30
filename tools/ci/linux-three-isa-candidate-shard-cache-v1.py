#!/usr/bin/env python3
"""Optional, fail-closed candidate shard cache. No compilation/link/runtime claims.

API: identity(target, compiler, assembler, config, owner_manifest, rows,
frozen, build, shard); store(cache, identity, staged, results);
verify(cache, expected_identity) -> manifest. Call identity again at restore
time using authenticated original GNU inputs. A rejected cache is a miss;
the caller must run MiniC + GNU AS, never GNU C fallback. Successful compile
shards may be stored before linking; runtime receipts are never cached here.

Conservatively refuses ALL .include/.incbin directives: their resource graph
is not inferred from C text. Such shards remain cold (safe cache misses).
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct

SCHEMA = "minic-gnu-candidate-shard-cache-v1"
MACHINES = {"x86_64": 62, "aarch64": 183, "loongarch64": 258}


class Rejected(ValueError):
    pass


def safe_file(path):
    path = Path(path)
    if any(p.is_symlink() for p in (path, *path.parents)):
        raise Rejected(f"symlink cache/input path: {path}")
    if not path.is_file() or not path.stat().st_size:
        raise Rejected(f"missing/empty cache/input: {path}")
    return path


def sha(path):
    h = hashlib.sha256()
    with safe_file(path).open("rb") as f:
        for block in iter(lambda: f.read(1048576), b""):
            h.update(block)
    return h.hexdigest()


def encoded(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def digest(value):
    return hashlib.sha256(encoded(value)).hexdigest()


def owner_path(value):
    p = PurePosixPath(value)
    if p.is_absolute() or ".." in p.parts or p.suffix != ".o" or str(p) != value:
        raise Rejected("unsafe/noncanonical object owner")
    return p


def no_external_assembly(path):
    # Search anywhere, including directives embedded inside C asm strings.
    if re.search(rb"\.(?:incbin|include)\b", safe_file(path).read_bytes(), re.I):
        raise Rejected("external assembly resources: object cache disabled")


def identity(target, compiler, assembler, config, owner_manifest, rows,
             frozen, build, shard):
    if target not in MACHINES or type(shard) is not int or not 0 <= shard < 7:
        raise Rejected("invalid target/shard")
    selected = []
    seen = set()
    for index, row in enumerate(rows):
        name = row["object"]
        p = owner_path(name)
        if name in seen or row["index"] != index or row["shard"] != index % 7:
            raise Rejected("duplicate/noncanonical owner partition")
        seen.add(name)
        if index % 7 != shard:
            continue
        inp = PurePosixPath(row["input"])
        if inp.is_absolute() or ".." in inp.parts or inp.parts[0] != "i":
            raise Rejected("unsafe frozen input")
        src = Path(frozen) / inp
        cmd = Path(build) / p.parent / ("." + p.name + ".cmd")
        if sha(src) != row["sha256"] or sha(cmd) != row["cmd_sha256"]:
            raise Rejected("original frozen input/command SHA mismatch")
        no_external_assembly(src)
        for field in ("minic_flags", "gnu_as_flags"):
            if not isinstance(row[field], list) or not all(isinstance(x, str) for x in row[field]):
                raise Rejected("invalid ordered command options")
        selected.append({k: row.get(k) for k in (
            "index", "shard", "object", "input", "sha256", "cmd_sha256",
            "minic_flags", "gnu_as_flags", "gnu_elf_abi")})
    if not selected:
        raise Rejected("empty shard")
    compiler = Path(compiler)
    result = dict(schema=SCHEMA, target=target, machine=MACHINES[target], shard=shard,
                  compiler_driver_sha256=sha(compiler),
                  compiler_cc_sha256=sha(compiler.parent / "minic-cc"),
                  gnu_as_binary_sha256=sha(assembler), config_sha256=sha(config),
                  owner_manifest_sha256=sha(owner_manifest), rows=selected,
                  helper_sha256=sha(Path(__file__)), external_asm_policy="reject")
    return result


def key(expected):
    return SCHEMA + "-" + digest(expected)


def check_elf(path, expected, row):
    head = safe_file(path).read_bytes()[:64]
    if (len(head) != 64 or head[:6] != b"\x7fELF\x02\x01" or
            struct.unpack_from("<HH", head, 16) != (1, expected["machine"])):
        raise Rejected("cached object not matching target ELF64 REL")
    if expected["target"] == "loongarch64":
        abi = row.get("gnu_elf_abi")
        if abi not in (1, 2, 3) or struct.unpack_from("<I", head, 48)[0] & 7 != abi:
            raise Rejected("cached LoongArch ELF ABI mismatch")


def store(cache, expected, staged, results):
    cache, staged = Path(cache), Path(staged)
    if cache.exists():
        raise Rejected("immutable cache already exists")
    by_name = {r["object"]: r for r in results}
    names = [r["object"] for r in expected["rows"]]
    if len(by_name) != len(results) or set(by_name) != set(names):
        raise Rejected("incomplete/duplicate shard compile results")
    entries = []
    # Fully validate before creating anything usable as a cache.
    for row in expected["rows"]:
        p = owner_path(row["object"])
        result = by_name[str(p)]
        if result.get("status") != "MINIC_S_GNU_AS_PASS":
            raise Rejected("only complete MiniC/GNU-AS pass shards are cacheable")
        asm, obj = staged / p.with_suffix(".s"), staged / p
        no_external_assembly(asm)
        check_elf(obj, expected, row)
        entry = dict(object=str(p), asm_sha256=sha(asm), obj_sha256=sha(obj))
        if any(result.get(f) != entry[f] for f in ("asm_sha256", "obj_sha256")):
            raise Rejected("compile attestation SHA mismatch")
        entries.append(entry)
    cache.mkdir(parents=True)
    for entry in entries:
        p = owner_path(entry["object"])
        for suffix in (".s", ".o"):
            dest = cache / "staged" / p.with_suffix(suffix)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(staged / p.with_suffix(suffix), dest)
    manifest = dict(schema=SCHEMA, key=key(expected), identity=expected,
                    entries=entries, compile_only_not_runtime_certified=True)
    (cache / "manifest.json").write_bytes(encoded(manifest))
    (cache / "manifest.sha256").write_text(sha(cache / "manifest.json") + "\n")
    verify(cache, expected)
    return manifest


def verify(cache, expected):
    cache = Path(cache)
    actual_sha = safe_file(cache / "manifest.sha256").read_text().strip()
    if actual_sha != sha(cache / "manifest.json"):
        raise Rejected("cache manifest SHA mismatch")
    manifest = json.loads((cache / "manifest.json").read_text())
    if (manifest.get("schema") != SCHEMA or manifest.get("identity") != expected or
            manifest.get("key") != key(expected) or
            manifest.get("compile_only_not_runtime_certified") is not True):
        raise Rejected("cache identity mismatch")
    entries = manifest.get("entries", [])
    if [e.get("object") for e in entries] != [r["object"] for r in expected["rows"]]:
        raise Rejected("cache ordered shard membership mismatch")
    allowed = {"manifest.json", "manifest.sha256"}
    for entry, row in zip(entries, expected["rows"]):
        p = owner_path(row["object"])
        for suffix, field in ((".s", "asm_sha256"), (".o", "obj_sha256")):
            relative = Path("staged") / p.with_suffix(suffix)
            allowed.add(relative.as_posix())
            if sha(cache / relative) != entry[field]:
                raise Rejected("cache artifact SHA mismatch")
        no_external_assembly(cache / "staged" / p.with_suffix(".s"))
        check_elf(cache / "staged" / p, expected, row)
    files = set()
    for p in cache.rglob("*"):
        if p.is_symlink():
            raise Rejected("symlink in cache")
        if p.is_file():
            files.add(p.relative_to(cache).as_posix())
    if files != allowed:
        raise Rejected("unexpected cache files")
    return manifest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=("key", "verify"))
    p.add_argument("--identity", type=Path, required=True,
                   help="fresh identity JSON produced by identity() API, not restored cache")
    p.add_argument("--cache", type=Path)
    a = p.parse_args()
    expected = json.loads(safe_file(a.identity).read_text())
    try:
        if a.mode == "key":
            print(key(expected))
        else:
            if a.cache is None:
                p.error("verify requires --cache")
            verify(a.cache, expected)
            print("CANDIDATE_SHARD_CACHE=PASS compile_only=true runtime_certified=false")
    except (ValueError, OSError, KeyError, TypeError) as exc:
        print(f"CANDIDATE_SHARD_CACHE=MISS reason={exc}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
