#!/usr/bin/env python3
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time


def load_contract(path: Path):
    rows = []
    for line in path.read_text().splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows


def canonical_contract_sha(path: Path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def context_id(args):
    payload = json.dumps(args, separators=(",", ":"), ensure_ascii=True).encode()
    return hashlib.sha256(payload).hexdigest()[:20]


def expand_arg(value: str, src: Path, out: Path):
    return value.replace("{SRC}", str(src)).replace("{OUT}", str(out))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", required=True)
    parser.add_argument("--src", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--gcc", default="riscv64-linux-gnu-gcc")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--linux-id", default="linux-6.6.143-rv64-defconfig")
    args = parser.parse_args()

    contract = Path(args.contract).resolve()
    src = Path(args.src).resolve()
    out = Path(args.out).resolve()
    corpus = Path(args.corpus).resolve()
    refs = corpus / "refs"
    predefs = corpus / "predefines"
    stderr_root = corpus / "gcc-stderr"

    rows = load_contract(contract)
    if not rows:
        raise SystemExit("MINIPP_REFERENCE_CORPUS empty-contract")

    shutil.rmtree(corpus, ignore_errors=True)
    refs.mkdir(parents=True)
    predefs.mkdir(parents=True)
    stderr_root.mkdir(parents=True)

    repo_root = Path(__file__).resolve().parents[2]
    predef_script = repo_root / "tests/preprocessor/build_reference_predefines.sh"

    contexts = {}
    for row in rows:
        key = context_id(row["predef_args"])
        contexts.setdefault(key, row["predef_args"])

    started = time.monotonic()
    for key, raw_args in sorted(contexts.items()):
        output = predefs / f"{key}.h"
        env = os.environ.copy()
        env["REAL_CC"] = args.gcc
        env["MINIPP_LINUX_SRC"] = str(src)
        env["MINIPP_PREDEFINES_OUT"] = str(output)
        command = [
            "bash",
            str(predef_script),
            *[expand_arg(value, src, out) for value in raw_args],
        ]
        subprocess.run(command, cwd=repo_root, env=env, check=True)

    def build_one(row):
        index = int(row["index"])
        source = src / row["source"]
        ref = refs / f"{index:04d}.gcc.i"
        err = stderr_root / f"{index:04d}.stderr"
        key = context_id(row["predef_args"])
        predef = predefs / f"{key}.h"
        pp_args = [expand_arg(value, src, out) for value in row["pp_args"]]
        command = [
            args.gcc,
            "-E",
            "-P",
            "-undef",
            "-x",
            "c",
            "-include",
            str(predef),
            *pp_args,
            str(source),
            "-o",
            str(ref),
        ]
        one_started = time.monotonic()
        proc = subprocess.run(
            command,
            cwd=out,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
        )
        elapsed = time.monotonic() - one_started
        err.write_text(proc.stderr)
        return {
            "index": index,
            "returncode": proc.returncode,
            "seconds": elapsed,
            "bytes": ref.stat().st_size if ref.is_file() else 0,
        }

    def build_many(selected_rows):
        built = []
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(build_one, row): row for row in selected_rows}
            for future in as_completed(futures):
                built.append(future.result())
        built.sort(key=lambda item: item["index"])
        return built

    results = build_many(rows)
    failures = [
        item for item in results
        if item["returncode"] != 0 or item["bytes"] == 0
    ]

    if failures:
        by_index = {int(row["index"]): row for row in rows}
        retry_rows = [by_index[item["index"]] for item in failures]
        targets = [row["object"] for row in retry_rows]
        print(
            "MINIPP_REFERENCE_CORPUS_KBUILD_RETRY "
            f"count={len(targets)} targets={','.join(targets)}"
        )
        subprocess.run(
            [
                "make",
                "-C",
                str(src),
                f"O={out}",
                "ARCH=riscv",
                "CROSS_COMPILE=riscv64-linux-gnu-",
                "-k",
                f"-j{max(1, min(args.jobs, 8))}",
                *targets,
            ],
            check=False,
        )

        retry_results = {item["index"]: item for item in build_many(retry_rows)}
        results = [
            retry_results.get(item["index"], item)
            for item in results
        ]
        failures = [
            item for item in results
            if item["returncode"] != 0 or item["bytes"] == 0
        ]

    if failures:
        for item in failures[:30]:
            err = stderr_root / f"{item['index']:04d}.stderr"
            first = ""
            if err.is_file():
                first = next(
                    (line.strip() for line in err.read_text(errors="replace").splitlines() if line.strip()),
                    "",
                )
            print(
                "MINIPP_REFERENCE_CORPUS_FAIL "
                f"index={item['index']} rc={item['returncode']} "
                f"bytes={item['bytes']} reason={first!r}"
            )
        raise SystemExit(
            f"MINIPP_REFERENCE_CORPUS failed={len(failures)} selected={len(rows)}"
        )

    shutil.copy2(contract, corpus / "contract.jsonl")
    gcc_version = subprocess.check_output(
        [args.gcc, "-dumpfullversion"], text=True
    ).strip()
    gcc_machine = subprocess.check_output(
        [args.gcc, "-dumpmachine"], text=True
    ).strip()

    digest_lines = []
    total_bytes = 0
    for item in results:
        path = refs / f"{item['index']:04d}.gcc.i"
        total_bytes += path.stat().st_size
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        digest_lines.append(f"{digest}  refs/{path.name}\n")
    (corpus / "sha256sums.txt").write_text("".join(digest_lines))

    meta = {
        "schema": 1,
        "linux_id": args.linux_id,
        "contract_sha256": canonical_contract_sha(contract),
        "selected": len(rows),
        "predef_contexts": len(contexts),
        "gcc_fullversion": gcc_version,
        "gcc_machine": gcc_machine,
        "total_reference_bytes": total_bytes,
        "build_seconds": time.monotonic() - started,
    }
    (corpus / "meta.json").write_text(
        json.dumps(meta, indent=2, sort_keys=True) + "\n"
    )

    print(
        "MINIPP_REFERENCE_CORPUS "
        f"selected={meta['selected']} predef_contexts={meta['predef_contexts']} "
        f"bytes={total_bytes} seconds={meta['build_seconds']:.3f} "
        f"gcc={gcc_version} machine={gcc_machine} "
        f"contract_sha256={meta['contract_sha256']}"
    )


if __name__ == "__main__":
    main()
