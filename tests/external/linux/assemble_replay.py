#!/usr/bin/env python3
"""Assemble MiniC Linux replay outputs into real RV64 relocatable objects."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import argparse
import json
import subprocess
import time


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--work", required=True, type=Path)
    p.add_argument("--cc", default="riscv64-linux-gnu-gcc")
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--march", default="rv64imac_zicsr_zifencei_zihintpause")
    p.add_argument("--mabi", default="lp64")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    if args.jobs <= 0:
        raise SystemExit("--jobs must be positive")

    manifest = args.work / "selected-tus.txt"
    asm_root = args.work / "minic-out"
    obj_root = args.work / "object-out"
    err_root = args.work / "assembler-stderr"
    if not manifest.is_file():
        raise SystemExit(f"missing replay selection: {manifest}")
    obj_root.mkdir(parents=True, exist_ok=True)
    err_root.mkdir(parents=True, exist_ok=True)

    entries: list[tuple[int, str, str, str]] = []
    for raw in manifest.read_text().splitlines():
        if not raw.strip():
            continue
        index, obj, preprocessed, source = raw.split("\t", 3)
        entries.append((int(index), obj, preprocessed, source))
    if not entries:
        raise SystemExit("empty replay selection")

    started = time.monotonic()

    def assemble(entry: tuple[int, str, str, str]) -> dict[str, object]:
        index, obj, preprocessed, source = entry
        asm = asm_root / f"{preprocessed}.s"
        out = obj_root / f"{index:04d}.o"
        err = err_root / f"{index:04d}.stderr"
        if not asm.is_file() or asm.stat().st_size == 0:
            return {
                "index": index,
                "object": obj,
                "input": preprocessed,
                "source": source,
                "status": "ASM_MISSING",
                "returncode": None,
                "seconds": 0.0,
                "message": f"missing MiniC assembly: {asm}",
            }
        one_started = time.monotonic()
        proc = subprocess.run(
            [
                args.cc,
                f"-march={args.march}",
                f"-mabi={args.mabi}",
                "-mcmodel=medany",
                "-mstrict-align",
                "-Wa,-mno-arch-attr",
                "-x",
                "assembler",
                "-c",
                str(asm),
                "-o",
                str(out),
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            errors="replace",
            check=False,
        )
        elapsed = time.monotonic() - one_started
        err.write_text(proc.stderr)
        if proc.returncode == 0 and out.is_file() and out.stat().st_size > 0:
            return {
                "index": index,
                "object": obj,
                "input": preprocessed,
                "source": source,
                "status": "PASS",
                "returncode": 0,
                "seconds": elapsed,
                "object_bytes": out.stat().st_size,
                "message": "",
            }
        first = next((line.strip() for line in proc.stderr.splitlines() if line.strip()), "")
        return {
            "index": index,
            "object": obj,
            "input": preprocessed,
            "source": source,
            "status": "FAIL",
            "returncode": proc.returncode,
            "seconds": elapsed,
            "message": first or f"assembler failed rc={proc.returncode}",
        }

    results: list[dict[str, object]] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        future_map = {pool.submit(assemble, e): e for e in entries}
        done = 0
        for future in as_completed(future_map):
            row = future.result()
            results.append(row)
            done += 1
            print(
                f"LINUX_ASSEMBLE_PROGRESS done={done} total={len(entries)} "
                f"index={row['index']} status={row['status']}",
                flush=True,
            )

    results.sort(key=lambda row: int(row["index"]))
    (args.work / "assemble-results.json").write_text(
        json.dumps(results, indent=2, sort_keys=True)
    )

    passed = sum(row["status"] == "PASS" for row in results)
    failed = len(results) - passed
    object_bytes = sum(int(row.get("object_bytes", 0)) for row in results)
    elapsed = time.monotonic() - started
    print(
        f"LINUX_ASSEMBLE_SUMMARY selected={len(results)} pass={passed} fail={failed} "
        f"object_bytes={object_bytes} seconds={elapsed:.3f}"
    )
    if failed:
        for row in results:
            if row["status"] != "PASS":
                print(
                    f"LINUX_ASSEMBLE_BLOCKER index={row['index']} input={row['input']} "
                    f"source={row['source']} status={row['status']} "
                    f"rc={row['returncode']} message={row['message']}",
                    flush=True,
                )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
