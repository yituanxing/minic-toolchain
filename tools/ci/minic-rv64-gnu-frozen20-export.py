#!/usr/bin/env python3
"""One-shot, cache-only GNU Linux 6.6.143 preprocessed C input export.

The historical 10/5/5 owner list is a measured MiniC compiler-cost cohort
from 2026-10-16. A GNU golden may use a narrower config than that historical
corpus. If an old owner is absent, choose a deterministic .o size-proxy from
the *actual* certified GNU C owner set and disclose that substitution.
There is no compiler invocation, source download, or cache rebuild here.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import re
import shutil
import sys


COUNTS = {"hot": 10, "middle": 5, "light": 5}
OWNER = re.compile(r"[A-Za-z0-9_./+\-]+\.o\Z")
SHA = re.compile(r"[0-9a-f]{64}\Z")
GNU_BASELINE_KEYS = {
    "schema", "linux_release", "linux_archive_sha256", "config_sha256",
    "gcc_version", "gcc_target", "gcc_binary_sha256",
    "gnu_ld_binary_sha256", "gnu_ar_binary_sha256", "gcc_object_count",
    "vmlinux_sha256", "image_sha256", "qemu_contract",
}


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def checked_owner(name: str) -> str:
    p = PurePosixPath(name)
    if (not OWNER.fullmatch(name) or p.is_absolute() or ".." in p.parts
            or "." in p.parts or "//" in name or len(p.parts) < 2):
        raise ValueError(f"unsafe GNU C owner: {name!r}")
    return name


def read_owners(file: Path, out: Path) -> dict[str, int]:
    entries = [x.strip() for x in file.read_text().splitlines()
               if x.strip() and not x.startswith("#")]
    if len(entries) != 2064 or len(set(entries)) != 2064:
        raise ValueError(f"exact 2064 GNU C owners required, found {len(entries)}")
    result = {}
    for name in entries:
        checked_owner(name)
        obj = out / name
        cmd = obj.parent / ("." + obj.name + ".cmd")
        if not obj.is_file() or not cmd.is_file() or obj.is_symlink() or cmd.is_symlink():
            raise ValueError(f"missing authentic GNU object/.cmd: {name}")
        # The existing GNU owner classifier checks that .cmd is truly C.
        # For bounded *.i targets demand the ordinary source-filename
        # identity as well: Kbuild foo.i then names the same foo.c source.
        source = obj.with_suffix(".c").relative_to(out).as_posix()
        cmd_text = cmd.read_text(encoding="utf-8", errors="replace")
        match = re.search(r"^source_[^\n]*\s*:=\s*(\S+\.c)\s*$",
                          cmd_text, flags=re.M)
        if match is not None and (
                match.group(1) == source
                or match.group(1).endswith("/" + source)):
            result[name] = obj.stat().st_size
    if len(result) < 20:
        raise ValueError(f"only {len(result)} conventional C owners usable as .i targets")
    return result


def parse_historical(file: Path) -> list[tuple[str, str, str]]:
    records = []
    seen = set()
    for line in file.read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 3 or fields[0] not in COUNTS:
            raise ValueError(f"invalid historical cohort row: {line!r}")
        group, owner, ms = fields
        checked_owner(owner)
        if owner in seen or not re.fullmatch(r"\d+\.\d+", ms):
            raise ValueError(f"invalid/duplicate historical owner: {owner}")
        seen.add(owner)
        records.append((group, owner, ms))
    if len(records) != 20 or any(sum(r[0] == group for r in records) != count
                                 for group, count in COUNTS.items()):
        raise ValueError("historical cohort must contain exactly 10/5/5 owners")
    return records


def select(args: argparse.Namespace) -> None:
    sizes = read_owners(args.owners, args.out)
    historical = parse_historical(args.historical)
    selected: list[tuple[str, str, str, str, int]] = []
    seen = set()
    for group, owner, ms in historical:
        if owner in sizes:
            selected.append((group, owner, "historical-wall", ms, sizes[owner]))
            seen.add(owner)

    by_size = sorted((size, name) for name, size in sizes.items())
    center = (len(by_size) - 1) / 2
    cohorts = {
        "hot": list(reversed(by_size)),
        "middle": [r for _, r in sorted(enumerate(by_size),
                                         key=lambda pair: (abs(pair[0] - center),
                                                           pair[1][1]))],
        "light": by_size,
    }
    for group, expected in COUNTS.items():
        missing = expected - sum(row[0] == group for row in selected)
        for size, owner in cohorts[group]:
            if missing == 0:
                break
            if owner in seen:
                continue
            selected.append((group, owner, "gnu-object-size-proxy", "-", size))
            seen.add(owner)
            missing -= 1
        if missing != 0:
            raise ValueError(f"insufficient distinct GNU C owners for {group}")
    selected.sort(key=lambda item: (list(COUNTS).index(item[0]),
                                    item[2] != "historical-wall", item[1]))
    if len(selected) != 20 or len(seen) != 20:
        raise ValueError("selection must be exactly 20 distinct GNU C owners")
    args.selection.parent.mkdir(parents=True, exist_ok=True)
    with args.selection.open("w") as dst:
        dst.write("group\towner\tselection_source\thistorical_ms\tgnu_object_bytes\n")
        for row in selected:
            dst.write("\t".join(map(str, row)) + "\n")
    for group in COUNTS:
        matched = sum(row[0] == group and row[2] == "historical-wall"
                      for row in selected)
        print(f"FROZEN20_SELECT group={group} count={COUNTS[group]} "
              f"historical={matched} proxy={COUNTS[group]-matched}")
    print("FROZEN20_SELECT=PASS original_C_universe=2064 selected=20 "
          "proxy_is_object_size_not_measured_cpu=true")


def parse_selection(file: Path) -> list[tuple[str, str, str, str, str]]:
    rows = file.read_text().splitlines()
    if not rows or rows[0] != ("group\towner\tselection_source"
                                "\thistorical_ms\tgnu_object_bytes"):
        raise ValueError("invalid export selection header")
    parsed = []
    seen = set()
    for row in rows[1:]:
        cols = row.split("\t")
        if len(cols) != 5 or cols[0] not in COUNTS:
            raise ValueError(f"invalid selected row: {row!r}")
        checked_owner(cols[1])
        if cols[1] in seen:
            raise ValueError("duplicate exported owner")
        seen.add(cols[1])
        parsed.append(tuple(cols))
    if len(parsed) != 20 or any(sum(r[0] == group for r in parsed) != count
                                for group, count in COUNTS.items()):
        raise ValueError("export must contain exactly 10/5/5 rows")
    return parsed


def pack(args: argparse.Namespace) -> None:
    records = parse_selection(args.selection)
    if args.artifact.exists():
        raise ValueError("refuse to overwrite a previous export artifact")
    inputs = args.artifact / "inputs"
    inputs.mkdir(parents=True)
    with (args.artifact / "SHA256SUMS").open("w") as hashes, \
         (args.artifact / "index.tsv").open("w") as index:
        index.write("group\towner\tselection_source\thistorical_ms"
                    "\tgnu_object_bytes\tgnu_cmd_sha256\tbytes\tsha256"
                    "\tartifact_path\n")
        for group, owner, source, historical_ms, obj_bytes in records:
            frozen = args.out / (owner[:-2] + ".i")
            cmd = (args.out / owner).parent / ("." + Path(owner).name + ".cmd")
            if (not frozen.is_file() or frozen.is_symlink()
                    or frozen.stat().st_size == 0):
                raise ValueError(f"preprocessed original GNU input missing: {frozen}")
            if not cmd.is_file() or cmd.is_symlink():
                raise ValueError(f"original GNU .o.cmd missing: {cmd}")
            destination_rel = Path("inputs") / (owner[:-2] + ".i")
            destination = args.artifact / destination_rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(frozen, destination)
            digest = hashlib.sha256(destination.read_bytes()).hexdigest()
            if not SHA.fullmatch(digest):
                raise ValueError("unexpected SHA-256 encoding")
            hashes.write(f"{digest}  {destination_rel.as_posix()}\n")
            index.write("\t".join((group, owner, source, historical_ms,
                                   obj_bytes, file_sha256(cmd),
                                   str(destination.stat().st_size), digest,
                                   destination_rel.as_posix())) + "\n")
    # A portable, credential-free identity for comparing later machine code
    # experiments with this exact cached GNU C universe. Archive *only*
    # whitelisted immutable build facts, never environment/runner metadata.
    raw = args.original_provenance.read_text()
    baseline = {}
    for line in raw.splitlines():
        key, sep, value = line.partition("=")
        if (not sep or key not in GNU_BASELINE_KEYS or key in baseline or
                not value or any(c in value for c in "\r\n\t")):
            raise ValueError(f"unapproved original GNU provenance: {key!r}")
        baseline[key] = value
    if (set(baseline) != GNU_BASELINE_KEYS
            or baseline["schema"] != "linux-gnu-exact-config-v1"
            or baseline["linux_release"] != "6.6.143"
            or baseline["qemu_contract"] != "full_and_p1_passed"
            or not baseline["gcc_object_count"].isdigit()
            or int(baseline["gcc_object_count"]) < 2064
            or not SHA.fullmatch(baseline["config_sha256"])
            or file_sha256(args.config) != baseline["config_sha256"]
            or not SHA.fullmatch(baseline["linux_archive_sha256"])):
        raise ValueError("original GNU baseline identity is incomplete/inconsistent")
    if (not re.fullmatch(r"[0-9a-f]{40}", args.head)
            or not re.fullmatch(r"[a-z0-9.-]+", args.source_key)
            or not re.fullmatch(r"[a-z0-9.-]+", args.gnu_key)):
        raise ValueError("invalid exact export HEAD/cache key encoding")
    lines = [
        "schema=minic-rv64-gnu-frozen20-export-v1",
        f"export_head={args.head}",
        f"original_linux_source_cache_key={args.source_key}",
        f"original_gnu_cache_key={args.gnu_key}",
        f"original_gnu_baseline_sha256={file_sha256(args.original_provenance)}",
        f"original_gnu_objects_manifest_sha256={file_sha256(args.object_manifest)}",
        f"frozen20_owner_index_sha256={file_sha256(args.artifact / 'index.tsv')}",
        f"frozen20_input_manifest_sha256={file_sha256(args.artifact / 'SHA256SUMS')}",
        "original_gnu_baseline_fields_begin",
    ]
    lines.extend(f"original_gnu_{key}={baseline[key]}" for key in sorted(baseline))
    lines.append("original_gnu_baseline_fields_end")
    (args.artifact / "provenance.txt").write_text("\n".join(lines) + "\n")
    print("FROZEN20_PACK=PASS frozen_i=20 sha256_entries=20")


def main() -> int:
    parser = argparse.ArgumentParser()
    actions = parser.add_subparsers(dest="action", required=True)
    s = actions.add_parser("select")
    s.add_argument("--out", type=Path, required=True)
    s.add_argument("--owners", type=Path, required=True)
    s.add_argument("--historical", type=Path, required=True)
    s.add_argument("--selection", type=Path, required=True)
    p = actions.add_parser("pack")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--selection", type=Path, required=True)
    p.add_argument("--artifact", type=Path, required=True)
    p.add_argument("--original-provenance", type=Path, required=True)
    p.add_argument("--object-manifest", type=Path, required=True)
    p.add_argument("--config", type=Path, required=True)
    p.add_argument("--source-key", required=True)
    p.add_argument("--gnu-key", required=True)
    p.add_argument("--head", required=True)
    args = parser.parse_args()
    try:
        {"select": select, "pack": pack}[args.action](args)
    except (OSError, ValueError) as error:
        print(f"FROZEN20_EXPORT=FAIL {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
