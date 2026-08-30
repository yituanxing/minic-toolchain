#!/usr/bin/env python3
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import re

OBJECT_RE = re.compile(r"(?:^|\s)-o\s+([^\s;]+\.o)(?=\s|;|$)")
SOURCE_RE = re.compile(r"(?:^|\s)([^\s;]+\.c)(?=\s|;|$)")


def source_relative(raw_source: str, src_root: Path, out_root: Path, obj_rel: str):
    raw = Path(raw_source.strip("'\""))
    candidates = []
    if raw.is_absolute():
        candidates.append(raw.resolve())
    else:
        candidates.append((out_root / raw).resolve())
        candidates.append((src_root / raw).resolve())
    candidates.append((src_root / (obj_rel[:-2] + ".c")).resolve())
    for candidate in candidates:
        try:
            return PurePosixPath(candidate.relative_to(src_root).as_posix()).as_posix()
        except ValueError:
            pass
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan", required=True)
    parser.add_argument("--src", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--limit", type=int, default=500)
    parser.add_argument("--tsv", required=True)
    parser.add_argument("--sources", required=True)
    args = parser.parse_args()

    plan = Path(args.plan)
    src_root = Path(args.src).resolve()
    out_root = Path(args.out).resolve()
    entries = []
    seen = set()

    for line in plan.read_text(errors="replace").splitlines():
        padded = f" {line} "
        if "riscv64-linux-gnu-" not in line or " -c " not in padded:
            continue
        sources = SOURCE_RE.findall(line)
        objects = OBJECT_RE.findall(line)
        if not sources or not objects:
            continue

        raw_obj = objects[-1].strip("'\"")
        obj = Path(raw_obj)
        if obj.is_absolute():
            try:
                rel_obj = obj.resolve().relative_to(out_root).as_posix()
            except ValueError:
                continue
        else:
            rel_obj = PurePosixPath(obj.as_posix()).as_posix()
        if rel_obj.startswith("./"):
            rel_obj = rel_obj[2:]
        if rel_obj.startswith(("scripts/", "tools/")) or "/scripts/" in rel_obj:
            continue
        if not rel_obj.endswith(".o") or rel_obj in seen:
            continue

        rel_source = source_relative(sources[-1], src_root, out_root, rel_obj)
        if rel_source is None:
            continue
        seen.add(rel_obj)
        entries.append((rel_obj, rel_source))

    selected = entries[:args.limit]
    if len(selected) != args.limit:
        raise SystemExit(
            f"MINIPP_LINUX_FREEZE expected={args.limit} actual={len(selected)} total={len(entries)}"
        )

    tsv = Path(args.tsv)
    source_manifest = Path(args.sources)
    tsv.parent.mkdir(parents=True, exist_ok=True)
    source_manifest.parent.mkdir(parents=True, exist_ok=True)

    with tsv.open("w") as output:
        output.write("index\tobject\tsource\n")
        for index, (obj, source) in enumerate(selected):
            output.write(f"{index}\t{obj}\t{source}\n")

    with source_manifest.open("w") as output:
        for _, source in selected:
            output.write(source + "\n")

    normalized = "".join(
        f"{index}\t{obj}\t{source}\n"
        for index, (obj, source) in enumerate(selected)
    ).encode()
    digest = hashlib.sha256(normalized).hexdigest()
    noncanonical = [
        (index, obj, source)
        for index, (obj, source) in enumerate(selected)
        if obj[:-2] + ".c" != source
    ]

    print(
        f"MINIPP_LINUX_FREEZE total_c_tus={len(entries)} selected={len(selected)} "
        f"noncanonical={len(noncanonical)} mapped_sha256={digest}"
    )
    for index, obj, source in noncanonical:
        print(
            f"MINIPP_LINUX_FREEZE_NONCANON index={index} "
            f"object={obj} source={source}"
        )


if __name__ == "__main__":
    main()
