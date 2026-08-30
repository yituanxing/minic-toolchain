#!/usr/bin/env python3
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import re
import shlex

OBJECT_RE = re.compile(r"(?:^|\s)-o\s+([^\s;]+\.o)(?=\s|;|$)")
SOURCE_RE = re.compile(r"(?:^|\s)([^\s;]+\.c)(?=\s|;|$)")

PAIR_PP_FLAGS = {"-D", "-U", "-I", "-isystem", "-include"}
PAIR_SKIP_FLAGS = {"-o", "-MF", "-MT", "-MQ"}
PREDEF_EXACT = {
    "-fshort-wchar",
    "-funsigned-char",
    "-fsigned-char",
    "-fno-PIE",
    "-fPIE",
    "-fno-pie",
    "-fpie",
    "-fPIC",
    "-fpic",
}


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


def normalize_arg(value: str, src_root: Path, out_root: Path):
    text = value
    src = str(src_root)
    out = str(out_root)
    if src in text:
        text = text.replace(src, "{SRC}")
    if out in text:
        text = text.replace(out, "{OUT}")
    return text


def extract_contract_args(line: str, src_root: Path, out_root: Path):
    try:
        tokens = shlex.split(line, posix=True)
    except ValueError as exc:
        raise SystemExit(f"MINIPP_LINUX_FREEZE shlex-error={exc} line={line[:240]!r}")

    compiler_index = None
    for index, token in enumerate(tokens):
        if token.endswith("riscv64-linux-gnu-gcc"):
            compiler_index = index
            break
    if compiler_index is None:
        raise SystemExit(f"MINIPP_LINUX_FREEZE compiler-token-missing line={line[:240]!r}")

    args = tokens[compiler_index + 1 :]
    pp_args = []
    predef_args = []
    index = 0
    while index < len(args):
        arg = args[index]

        if arg in PAIR_PP_FLAGS:
            if index + 1 >= len(args):
                raise SystemExit(f"MINIPP_LINUX_FREEZE dangling-flag={arg}")
            pp_args.append(arg)
            pp_args.append(normalize_arg(args[index + 1], src_root, out_root))
            index += 2
            continue

        if arg in PAIR_SKIP_FLAGS:
            index += 2
            continue

        if arg.startswith("-D") or arg.startswith("-U") or arg.startswith("-I"):
            pp_args.append(normalize_arg(arg, src_root, out_root))
            index += 1
            continue

        if arg == "-nostdinc" or arg.startswith("-isystem") or arg.startswith("-include"):
            pp_args.append(normalize_arg(arg, src_root, out_root))
            index += 1
            continue

        if arg.startswith("-Wp,") or arg in {"-MMD", "-MD", "-MP", "-c"}:
            index += 1
            continue

        if (
            arg.startswith("-m")
            or arg.startswith("-std=")
            or arg in PREDEF_EXACT
            or arg in {"-ffreestanding", "-fhosted"}
        ):
            predef_args.append(arg)
            index += 1
            continue

        index += 1

    return pp_args, predef_args


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan", required=True)
    parser.add_argument("--src", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--limit", type=int, default=500)
    parser.add_argument("--tsv", required=True)
    parser.add_argument("--sources", required=True)
    parser.add_argument("--contract")
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
        try:
            if obj.is_absolute():
                rel_obj = obj.resolve().relative_to(out_root).as_posix()
            else:
                rel_obj = (out_root / obj).resolve().relative_to(out_root).as_posix()
        except ValueError:
            continue

        if rel_obj.startswith(("scripts/", "tools/")) or "/scripts/" in rel_obj:
            continue
        if not rel_obj.endswith(".o") or rel_obj in seen:
            continue

        rel_source = source_relative(sources[-1], src_root, out_root, rel_obj)
        if rel_source is None:
            continue

        pp_args, predef_args = extract_contract_args(line, src_root, out_root)
        seen.add(rel_obj)
        entries.append(
            {
                "object": rel_obj,
                "source": rel_source,
                "pp_args": pp_args,
                "predef_args": predef_args,
            }
        )

    selected = entries[: args.limit]
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
        for index, entry in enumerate(selected):
            output.write(f"{index}\t{entry['object']}\t{entry['source']}\n")

    with source_manifest.open("w") as output:
        for entry in selected:
            output.write(entry["source"] + "\n")

    normalized_rows = "".join(
        f"{index}\t{entry['object']}\t{entry['source']}\n"
        for index, entry in enumerate(selected)
    ).encode()
    mapped_digest = hashlib.sha256(normalized_rows).hexdigest()

    contract_digest = None
    if args.contract:
        contract = Path(args.contract)
        contract.parent.mkdir(parents=True, exist_ok=True)
        canonical_lines = []
        with contract.open("w") as output:
            for index, entry in enumerate(selected):
                row = {"index": index, **entry}
                line = json.dumps(row, sort_keys=True, separators=(",", ":"))
                output.write(line + "\n")
                canonical_lines.append(line + "\n")
        contract_digest = hashlib.sha256("".join(canonical_lines).encode()).hexdigest()

    noncanonical = [
        (index, entry["object"], entry["source"])
        for index, entry in enumerate(selected)
        if entry["object"][:-2] + ".c" != entry["source"]
    ]
    predef_contexts = {
        tuple(entry["predef_args"])
        for entry in selected
    }

    headline = (
        f"MINIPP_LINUX_FREEZE total_c_tus={len(entries)} selected={len(selected)} "
        f"noncanonical={len(noncanonical)} predef_contexts={len(predef_contexts)} "
        f"mapped_sha256={mapped_digest}"
    )
    if contract_digest is not None:
        headline += f" contract_sha256={contract_digest}"
    print(headline)

    for index, obj, source in noncanonical:
        print(
            f"MINIPP_LINUX_FREEZE_NONCANON index={index} "
            f"object={obj} source={source}"
        )


if __name__ == "__main__":
    main()
