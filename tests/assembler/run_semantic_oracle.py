#!/usr/bin/env python3
from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import importlib.util
import json
from pathlib import Path
import subprocess
import time


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--input-root", type=Path, required=True)
    p.add_argument("--output-root", type=Path, required=True)
    p.add_argument("--minias", type=Path, required=True)
    p.add_argument("--oracle", type=Path, required=True)
    p.add_argument("--gcc", default="riscv64-linux-gnu-gcc")
    p.add_argument("--cwd", type=Path)
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--expected", type=int)
    p.add_argument("--native35", action="store_true")
    p.add_argument("--require-equal", action="store_true")
    return p.parse_args()


def target_for(rel: str, native35: bool) -> tuple[str, str]:
    if "compat_vdso/" in rel:
        return "rv32imafdc_zicsr_zifencei", "ilp32"
    if native35:
        return "rv64imafdc_zicsr_zifencei_zihintpause", "lp64"
    return "rv64imac_zicsr_zifencei_zihintpause", "lp64"


def first_line(text: str) -> str:
    return next((line.strip() for line in text.splitlines() if line.strip()), "-")


def load_oracle(path: Path):
    spec = importlib.util.spec_from_file_location("minias_elf_semantic_compare", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load semantic oracle: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    args = parse_args()
    if args.jobs <= 0:
        raise SystemExit("--jobs must be positive")

    root = args.input_root.resolve()
    out = args.output_root.resolve()
    cwd = (args.cwd or Path.cwd()).resolve()
    minias = args.minias.resolve()
    oracle = args.oracle.resolve()
    oracle_module = load_oracle(oracle)
    files = sorted(root.rglob("*.s"))

    if args.expected is not None and len(files) != args.expected:
        raise SystemExit(f"expected {args.expected} .s inputs, got {len(files)}")
    if not files:
        raise SystemExit("no .s inputs")

    out.mkdir(parents=True, exist_ok=True)
    (out / "reference").mkdir(exist_ok=True)
    (out / "candidate").mkdir(exist_ok=True)
    (out / "semantic").mkdir(exist_ok=True)
    (out / "stderr").mkdir(exist_ok=True)

    def one(inp: Path) -> dict[str, object]:
        rel = inp.relative_to(root).as_posix()
        march, mabi = target_for(rel, args.native35)
        key = rel[:-2] if rel.endswith(".s") else rel
        ref = out / "reference" / f"{key}.o"
        cand = out / "candidate" / f"{key}.o"
        semantic = out / "semantic" / f"{key}.json"
        gnu_err = out / "stderr" / f"{key}.gnu.err"
        mini_err = out / "stderr" / f"{key}.minias.err"
        for p in (ref, cand, semantic, gnu_err, mini_err):
            p.parent.mkdir(parents=True, exist_ok=True)

        gnu = subprocess.run(
            [
                args.gcc,
                f"-march={march}",
                f"-mabi={mabi}",
                "-mcmodel=medany",
                "-mstrict-align",
                "-Wa,-mno-arch-attr",
                "-x", "assembler", "-c",
                str(inp), "-o", str(ref),
            ],
            cwd=cwd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
        )
        gnu_err.write_text(gnu.stderr)
        if gnu.returncode != 0:
            return {"status": "GNU_FAIL", "path": rel, "message": first_line(gnu.stderr)}

        mini = subprocess.run(
            [str(minias), f"-march={march}", f"-mabi={mabi}", "-o", str(cand), str(inp)],
            cwd=cwd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
        )
        mini_err.write_text(mini.stderr)
        if mini.returncode != 0:
            return {"status": "MINIAS_FAIL", "path": rel, "message": first_line(mini.stderr)}

        try:
            data = oracle_module.compare_paths(ref, cand)
            semantic.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
        except Exception as exc:
            return {"status": "ORACLE_ERROR", "path": rel, "message": str(exc)}

        failed_dims = sorted(k for k, ok in data["dimensions"].items() if not ok)
        return {
            "status": "PASS" if bool(data["equal"]) else "DIFF",
            "path": rel,
            "strict_equal": bool(data.get("strict_equal", False)),
            "failed_dimensions": failed_dims,
            "message": ",".join(failed_dims) if failed_dims else "-",
        }

    started = time.monotonic()
    rows: list[dict[str, object]] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(one, p): p for p in files}
        done = 0
        for fut in as_completed(futures):
            row = fut.result()
            rows.append(row)
            done += 1
            print(
                f"MINIAS_SEMANTIC_PROGRESS done={done} total={len(files)} "
                f"status={row['status']} path={row['path']}",
                flush=True,
            )

    rows.sort(key=lambda x: str(x["path"]))
    with (out / "results.tsv").open("w") as f:
        for row in rows:
            f.write(
                f"{row['status']}\t{row['path']}\t"
                f"{1 if row.get('strict_equal') else 0}\t{row.get('message','-')}\n"
            )

    status = Counter(str(row["status"]) for row in rows)
    dimensions = Counter()
    for row in rows:
        for dimension in row.get("failed_dimensions", []):
            dimensions[str(dimension)] += 1

    strict = sum(bool(row.get("strict_equal")) for row in rows)
    elapsed = time.monotonic() - started
    lines = [
        "MINIAS_SEMANTIC_SUMMARY "
        f"selected={len(rows)} semantic_equal={status['PASS']} semantic_diff={status['DIFF']} "
        f"strict_equal={strict} gnu_fail={status['GNU_FAIL']} "
        f"minias_fail={status['MINIAS_FAIL']} oracle_error={status['ORACLE_ERROR']} "
        f"seconds={elapsed:.3f}",
        "MINIAS_SEMANTIC_DIMENSIONS",
    ]
    lines.extend(
        [f"  {count:5d} {name}" for name, count in dimensions.most_common()]
        or ["  none"]
    )
    summary = "\n".join(lines) + "\n"
    (out / "summary.txt").write_text(summary)
    print(summary, end="")

    infrastructure_failures = (
        status["GNU_FAIL"] + status["MINIAS_FAIL"] + status["ORACLE_ERROR"]
    )
    if infrastructure_failures:
        return 2
    if args.require_equal and status["DIFF"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
