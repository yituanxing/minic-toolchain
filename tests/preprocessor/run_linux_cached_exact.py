#!/usr/bin/env python3
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import subprocess
import time
import shutil


def load_contract(path: Path):
    rows = []
    for line in path.read_text().splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows


def context_id(args):
    payload = json.dumps(args, separators=(",", ":"), ensure_ascii=True).encode()
    return hashlib.sha256(payload).hexdigest()[:20]


def expand_arg(value: str, src: Path, out: Path):
    return value.replace("{SRC}", str(src)).replace("{OUT}", str(out))


def resolve_contract_source(value: str, src: Path, out: Path):
    if value.startswith("{OUT}/"):
        return out / value[len("{OUT}/") :]
    if value.startswith("{SRC}/"):
        return src / value[len("{SRC}/") :]
    return src / value


def read_indices(path):
    if path is None:
        return None
    values = []
    for line in Path(path).read_text().splitlines():
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        values.append(int(text))
    return values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", required=True)
    parser.add_argument("--corpus", required=True)
    parser.add_argument("--src", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--minipp", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--indices")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--label", default="product")
    parser.add_argument("--logical-root", default="/tmp/minipp-linux-first500")
    args = parser.parse_args()

    contract_path = Path(args.contract).resolve()
    corpus = Path(args.corpus).resolve()
    src = Path(args.src).resolve()
    out = Path(args.out).resolve()
    minipp = Path(args.minipp).resolve()
    work = Path(args.work).resolve()
    logical_root = Path(args.logical_root)
    generated_root = corpus / "generated-out"
    if generated_root.is_dir():
        shutil.copytree(generated_root, out, dirs_exist_ok=True)
    shutil.rmtree(logical_root, ignore_errors=True)
    logical_root.mkdir(parents=True, exist_ok=True)
    logical_src = logical_root / "src"
    logical_out = logical_root / "out"
    logical_src.symlink_to(src, target_is_directory=True)
    logical_out.symlink_to(out, target_is_directory=True)

    meta = json.loads((corpus / "meta.json").read_text())
    contract_sha = hashlib.sha256(contract_path.read_bytes()).hexdigest()
    if meta["contract_sha256"] != contract_sha:
        raise SystemExit(
            "MINIPP_CACHED_REPLAY contract-mismatch "
            f"expected={meta['contract_sha256']} actual={contract_sha}"
        )

    rows = load_contract(contract_path)
    by_index = {int(row["index"]): row for row in rows}
    selected_indices = read_indices(args.indices)
    if selected_indices is None:
        selected = rows
    else:
        missing = [index for index in selected_indices if index not in by_index]
        if missing:
            raise SystemExit(f"MINIPP_CACHED_REPLAY missing-indices={missing}")
        selected = [by_index[index] for index in selected_indices]

    mini_root = work / "mini"
    stderr_root = work / "stderr"
    diff_root = work / "diff"
    mini_root.mkdir(parents=True, exist_ok=True)
    stderr_root.mkdir(parents=True, exist_ok=True)
    diff_root.mkdir(parents=True, exist_ok=True)

    def run_one(row):
        index = int(row["index"])
        source = resolve_contract_source(row["source"], logical_src, logical_out)
        ref = corpus / "refs" / f"{index:04d}.gcc.i"
        predef = corpus / "predefines" / f"{context_id(row['predef_args'])}.h"
        mini = mini_root / f"{index:04d}.mini.i"
        err = stderr_root / f"{index:04d}.stderr"
        pp_args = [expand_arg(value, logical_src, logical_out) for value in row["pp_args"]]
        command = [
            str(minipp),
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
            str(mini),
        ]
        started = time.monotonic()
        proc = subprocess.run(
            command,
            cwd=logical_out,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
        )
        elapsed = time.monotonic() - started
        err.write_text(proc.stderr)

        if proc.returncode != 0:
            status = "MINIPP_FAIL"
        elif not mini.is_file():
            status = "MINIPP_FAIL"
        elif ref.read_bytes() == mini.read_bytes():
            status = "EXACT"
        else:
            status = "DIFF"
            ref_lines = ref.read_text(errors="replace").splitlines(keepends=True)
            mini_lines = mini.read_text(errors="replace").splitlines(keepends=True)
            diff = difflib.unified_diff(
                ref_lines,
                mini_lines,
                fromfile=str(ref),
                tofile=str(mini),
                n=3,
            )
            (diff_root / f"{index:04d}.diff").write_text("".join(diff))

        return {
            "index": index,
            "object": row["object"],
            "source": row["source"],
            "status": status,
            "returncode": proc.returncode,
            "seconds": elapsed,
        }

    started = time.monotonic()
    results = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_one, row): row for row in selected}
        for future in as_completed(futures):
            results.append(future.result())
    results.sort(key=lambda row: row["index"])

    exact = sum(row["status"] == "EXACT" for row in results)
    diff = sum(row["status"] == "DIFF" for row in results)
    fail = sum(row["status"] == "MINIPP_FAIL" for row in results)
    elapsed = time.monotonic() - started

    (work / "results.json").write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n"
    )
    with (work / "results.tsv").open("w") as output:
        output.write("index\tstatus\tobject\tsource\tseconds\treturncode\n")
        for row in results:
            output.write(
                f"{row['index']}\t{row['status']}\t{row['object']}\t"
                f"{row['source']}\t{row['seconds']:.6f}\t{row['returncode']}\n"
            )

    with (work / "failure-pool.tsv").open("w") as output:
        output.write("index\tobject\tsource\tstatus\n")
        for row in results:
            if row["status"] != "EXACT":
                output.write(
                    f"{row['index']}\t{row['object']}\t{row['source']}\t"
                    f"{row['status']}\n"
                )

    times = sorted(row["seconds"] for row in results)
    median = times[len(times) // 2] if times else 0.0
    p95 = times[min(len(times) - 1, int(len(times) * 0.95))] if times else 0.0

    headline = (
        f"MINIPP_CACHED_BATCH label={args.label} selected={len(results)} "
        f"exact={exact} minipp_fail={fail} diff={diff} "
        f"wall_seconds={elapsed:.3f} median_tu_seconds={median:.6f} "
        f"p95_tu_seconds={p95:.6f}"
    )
    print(headline)
    (work / "headline.txt").write_text(headline + "\n")

    for row in results:
        if row["status"] != "EXACT":
            print(
                f"MINIPP_CACHED_RESULT index={row['index']} "
                f"object={row['object']} source={row['source']} "
                f"status={row['status']}"
            )


if __name__ == "__main__":
    main()
