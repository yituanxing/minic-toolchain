#!/usr/bin/env python3
"""Build one pinned compiler-rt *source set* with all four MiniC targets.

No per-operation patch files, hidden GNU fallback, or host-ABI objects. The
reference route uses Clang, and the MiniC route uses MiniC to compile the exact
same target-preprocessed .i. Target assembler/archiver remain external.
Only the RV64I profile supplies primitive libgcc .S to break self-recursion.
Reports are always written, including on failure; no partial archive is made.
"""
from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "runtime" / "manifest.json"


def read_manifest() -> dict:
    data = json.loads(MANIFEST.read_text())
    items = [item for group in data["groups"].values() for item in group]
    assert len(set(items)) == len(items), "duplicate upstream object name"
    for item in items:
        source = ROOT / data["shared_source_root"] / item
        if not source.is_file():
            raise FileNotFoundError(str(source))
    return data


def execute(argv: list[str], output: Path, timeout: int = 160) -> bool:
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with output.open("w") as stream:
            p = subprocess.run(argv, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                               timeout=timeout, check=False)
        return p.returncode == 0
    except (OSError, subprocess.TimeoutExpired) as error:
        with output.open("a") as stream:
            print(f"RUNTIME_TOOL_ERROR={error}", file=stream)
        return False


def target_flags(config: dict) -> list[str]:
    return ["--target=" + config["triple"], *config["clang_flags"]]


def source_compile(args: argparse.Namespace, manifest: dict, target: str,
                   name: str, output: Path) -> dict:
    config = manifest["targets"][target]
    base = output / "objects" / Path(name).stem
    pp_file = base.with_suffix(".i")
    asm_file = base.with_suffix(".s")
    obj_file = base.with_suffix(".o")
    source = ROOT / manifest["shared_source_root"] / name
    includes = [
        "-I" + str(ROOT / manifest["shared_source_root"]),
        "-I" + str(ROOT / "third_party/gcc-riscv-14.2.0"),
        "-isystem", str(ROOT / manifest["shared_source_root"] / "freestanding-clang"),
    ]
    cpp_cmd = [args.clang, *target_flags(config), "-E", "-P", "-std=gnu11",
               "-ffreestanding", "-fno-builtin", "-nostdinc", *includes,
               str(source), "-o", str(pp_file)]
    result = dict(source=name, stage="preprocess", success=False, object=str(obj_file))
    if not execute(cpp_cmd, output / "logs" / (name + ".cpp.log")) or not pp_file.is_file():
        return result
    if args.backend == "minic":
        cc_cmd = [str(args.minic), "--target=" + config["minic_target"],
                  *config["minic_flags"], "-S", str(pp_file), "-o", str(asm_file)]
        if not execute(cc_cmd, output / "logs" / (name + ".minic.log")) or not asm_file.is_file():
            result["stage"] = "minic"
            return result
        asm_cmd = [args.clang, *target_flags(config), "-c", "-x", "assembler",
                   str(asm_file), "-o", str(obj_file)]
        if not execute(asm_cmd, output / "logs" / (name + ".as.log")) or not obj_file.is_file():
            result["stage"] = "assemble"
            return result
    else:
        cc_cmd = [args.clang, *target_flags(config), "-O2", "-fno-builtin",
                  "-ffreestanding", "-fno-pic", "-fno-stack-protector", "-fno-lto",
                  "-c", "-x", "c-cpp-output", str(pp_file), "-o", str(obj_file)]
        if not execute(cc_cmd, output / "logs" / (name + ".reference.log")) or not obj_file.is_file():
            result["stage"] = "reference"
            return result
    result["stage"] = "PASS"
    result["success"] = True
    return result


def asm_primitive(args: argparse.Namespace, manifest: dict, target: str,
                  source_rel: str, output: Path) -> dict:
    config = manifest["targets"][target]
    p = ROOT / source_rel
    object_file = output / "objects" / (p.stem + "-primitive.o")
    include = "-I" + str(p.parent)
    cmd = [args.clang, *target_flags(config), "-c", "-x", "assembler-with-cpp",
           include, str(p), "-o", str(object_file)]
    stage = "primitive-asm"
    if target == "riscv64" and shutil.which(args.rv64_gnu_gcc):
        cmd = [args.rv64_gnu_gcc, "-march=rv64i", "-mabi=lp64", include,
               "-c", str(p), "-o", str(object_file)]
        stage = "primitive-gnu-as"
    ok = execute(cmd, output / "logs" / (p.name + ".as.log"))
    return dict(source=source_rel, stage="PASS" if ok else stage,
                success=ok, object=str(object_file))


def check_symbol_graph(args: argparse.Namespace, objects: list[Path],
                       output: Path) -> dict:
    defined: set[str] = set()
    undefined: set[str] = set()
    for obj in objects:
        defs = subprocess.run([args.nm, "-g", "--defined-only", str(obj)],
                              capture_output=True, text=True, check=False)
        unresolved = subprocess.run([args.nm, "-u", str(obj)],
                                    capture_output=True, text=True, check=False)
        if defs.returncode or unresolved.returncode:
            return dict(status="nm-unavailable", object=str(obj))
        for line in defs.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 2:
                defined.add(parts[-1])
        for line in unresolved.stdout.splitlines():
            parts = line.split()
            if parts:
                undefined.add(parts[-1])
    return dict(status="ok", defined_count=len(defined),
                unresolved=sorted(undefined - defined),
                defined_symbols=sorted(defined))


def run_target(args: argparse.Namespace, manifest: dict, target: str) -> dict:
    started = time.monotonic()
    group_names = args.groups.split(",") if args.groups != "all" else list(manifest["groups"])
    names = [name for group in group_names for name in manifest["groups"][group]]
    output = (args.build_dir / args.backend / target).resolve()
    (output / "objects").mkdir(parents=True, exist_ok=True)
    rows: list[dict] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(source_compile, args, manifest, target, name, output): name
                   for name in names}
        for future in as_completed(futures):
            try:
                row = future.result()
            except Exception as exc:
                row = dict(source=futures[future], stage="exception",
                           success=False, error=str(exc))
            rows.append(row)
    rows.sort(key=lambda x: x["source"])
    if not args.no_primitives:
        for source_rel in manifest["targets"][target].get("assembly_primitives", []):
            rows.append(asm_primitive(args, manifest, target, source_rel, output))
    objects = [Path(row["object"]) for row in rows if row["success"]]
    success = bool(rows) and all(row["success"] for row in rows)
    if success:
        archive = output / "libminic-rt.a"
        archive_cmd = [args.ar, "rcs", str(archive), *[str(x) for x in objects]]
        success = execute(archive_cmd, output / "logs" / "ar.log") and archive.is_file()
    else:
        archive = None
    graph = check_symbol_graph(args, objects, output) if success else {"status": "incomplete"}
    if args.require_closure and graph.get("unresolved"):
        success = False
    if args.backend == "minic" and target == "riscv64":
        # Do not silently replace the RV64I ISA verification with file counts.
        audit = ROOT / "tools/ci/audit-rv64i-asm-origin.py"
        assembly_root = output / "objects"
        p = subprocess.run([sys.executable, str(audit), "--root", str(assembly_root),
                            "--json-out", str(output / "rv64i-origin.json"),
                            "--opcode-tsv-out", str(output / "rv64i-opcodes.tsv"),
                            "--fail-on-compiler-non-i"],
                           cwd=ROOT, text=True, capture_output=True, check=False)
        (output / "logs" / "rv64i-scan.log").write_text(p.stdout + p.stderr)
        if p.returncode:
            success = False
    report = {
        "target": target, "backend": args.backend, "selected_groups": group_names,
        "source_count": len(rows), "built_count": len(objects),
        "complete": success, "archive": str(archive) if archive and archive.is_file() else None,
        "symbol_graph": graph, "seconds": round(time.monotonic() - started, 2),
        "sources": rows,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2))
    print("RUNTIME_TARGET={} mode={} success={} compiled={}/{} archive={} "
          "unresolved={}".format(target, args.backend, success, len(objects),
                                  len(rows), report["archive"],
                                  len(graph.get("unresolved", []))), flush=True)
    for row in rows:
        if not row["success"]:
            print("RUNTIME_FAIL={} file={} stage={}".format(
                target, row["source"], row["stage"]), flush=True)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print-sources", action="store_true")
    parser.add_argument("--backend", choices=("minic", "reference"), default="minic")
    parser.add_argument("--targets", default="all",
                        help="all or comma-separated manifest target keys")
    parser.add_argument("--groups", default="all",
                        help="all or comma-separated manifest groups")
    parser.add_argument("--minic", type=Path, default=ROOT / "build/release/bin/minic-cc")
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--ar", default="llvm-ar")
    parser.add_argument("--nm", default="llvm-nm")
    parser.add_argument("--rv64-gnu-gcc", default="riscv64-linux-gnu-gcc")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--no-primitives", action="store_true")
    parser.add_argument("--require-closure", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/runtime-fourarch")
    args = parser.parse_args()
    manifest = read_manifest()
    if args.print_sources:
        print(" ".join(name for group in manifest["groups"].values() for name in group))
        return 0
    targets = list(manifest["targets"]) if args.targets == "all" else args.targets.split(",")
    for target in targets:
        if target not in manifest["targets"]:
            parser.error("unknown target: " + target)
    groups = list(manifest["groups"]) if args.groups == "all" else args.groups.split(",")
    for group in groups:
        if group not in manifest["groups"]:
            parser.error("unknown group: " + group)
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.backend == "minic" and not args.minic.exists():
        parser.error("MiniC executable missing: " + str(args.minic))
    all_reports = [run_target(args, manifest, target) for target in targets]
    passed = all(r["complete"] for r in all_reports)
    summary = dict(complete=passed, backend=args.backend, targets=all_reports)
    args.build_dir.mkdir(parents=True, exist_ok=True)
    (args.build_dir / ("summary-" + args.backend + ".json")).write_text(
        json.dumps(summary, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
