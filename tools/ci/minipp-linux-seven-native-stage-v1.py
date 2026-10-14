#!/usr/bin/env python3
"""Seven independent real-Linux PP -> MiniC -> MiniAS provenance probes.

This is a staged PP integration gate, NOT full-Kbuild or QEMU acceptance.
Each of the seven existing gcc-shards runners selects actual Kbuild C
objects from the SHA-attested GNU golden pool. GNU is used only as the
reference for macro context and exact preprocessed bytes. The compiler
takes MiniPP-produced .i, never the GNU reference .i. No golden object is
overwritten; failure samples and ELF attestations are retained per shard.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests/preprocessor"))
from run_linux_native_full_production import extract, predef_args  # noqa: E402


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pp_args(args: list[str]) -> list[str]:
    """Match existing audited linux_kbuild_pp_cc.sh argument normalization."""
    out: list[str] = []
    takes_value = {"-D", "-U", "-I", "-isystem", "-include"}
    i = 0
    while i < len(args):
        a = args[i]
        if a in takes_value:
            if i + 1 >= len(args):
                raise ValueError(f"truncated Kbuild option: {a}")
            out.extend((a, args[i + 1]))
            i += 2
            continue
        if a.startswith(("-D", "-U", "-I", "-isystem", "-include")) or a == "-nostdinc":
            out.append(a)
        i += 1
    return out


def run(cmd: list[str], cwd: Path, output: Path) -> None:
    p = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, errors="replace")
    output.write_text("COMMAND " + json.dumps(cmd) + "\n"
                      + f"returncode={p.returncode}\nstdout:\n{p.stdout[-4000:]}"
                      + f"\nstderr:\n{p.stderr[-11000:]}\n")
    if p.returncode:
        raise RuntimeError(f"returncode={p.returncode}: {cmd[0]}")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--src", type=Path, required=True)
    p.add_argument("--owner-list", type=Path, required=True)
    p.add_argument("--stage", type=Path, required=True)
    p.add_argument("--minipp", type=Path, required=True)
    p.add_argument("--minic-cc", type=Path, required=True)
    p.add_argument("--minias", type=Path, required=True)
    p.add_argument("--shard", type=int, required=True)
    p.add_argument("--shards", type=int, required=True)
    p.add_argument("--sample-per-shard", type=int, default=2)
    args = p.parse_args()
    if args.shards != 7 or not 0 <= args.shard < 7:
        raise SystemExit("MINIPP_SEVEN=FAIL invalid-seven-runner-partition")
    if not 1 <= args.sample_per_shard <= 256:
        raise SystemExit("MINIPP_SEVEN=FAIL invalid-sample-limit")
    out, src, stage = args.out.resolve(), args.src.resolve(), args.stage.resolve()
    stage.mkdir(parents=True, exist_ok=True)
    owner_names = [s.strip() for s in args.owner_list.read_text().splitlines()
                   if s.strip() and not s.lstrip().startswith("#")]
    if not 1900 <= len(owner_names) <= 3000 or len(set(owner_names)) != len(owner_names):
        raise SystemExit(f"MINIPP_SEVEN=FAIL invalid-real-owner-universe:{len(owner_names)}")
    entries = {row["object"]: row for row in extract(out, src)}
    missing = [name for name in owner_names if name not in entries]
    if missing:
        raise SystemExit("MINIPP_SEVEN=FAIL missing-real-Kbuild-C-producers:"
                         + ",".join(missing[:8]))
    selected = owner_names[args.shard::args.shards][:args.sample_per_shard]
    if len(selected) != args.sample_per_shard:
        raise SystemExit("MINIPP_SEVEN=FAIL incomplete-worker-selection")
    print(f"MINIPP_SEVEN_PARTITION=PASS worker={args.shard}/7 "
          f"frozen_owners={len(owner_names)} selected={len(selected)} "
          "compiler_input=MiniPP.i GNU_reference_only=true", flush=True)
    rows = []
    predefs: dict[tuple[str, ...], Path] = {}
    for object_name in selected:
        row = entries[object_name]
        key = hashlib.sha256(object_name.encode()).hexdigest()[:16]
        item = stage / key
        item.mkdir(exist_ok=True)
        old_sha = sha(out / object_name)
        result = dict(object=object_name, source=row["source"], status="FAIL",
                      frozen_original_sha=old_sha)
        rows.append(result)
        try:
            context = predef_args(row["args"])
            if context not in predefs:
                macro_path = stage / ("predefs-" +
                    hashlib.sha256(json.dumps(context).encode()).hexdigest()[:16] + ".h")
                env = os.environ.copy()
                env.update(REAL_CC="riscv64-linux-gnu-gcc",
                           MINIPP_LINUX_SRC=str(src),
                           MINIPP_PREDEFINES_OUT=str(macro_path))
                proc = subprocess.run(
                    ["bash", "tests/preprocessor/build_reference_predefines.sh",
                     *context], cwd=Path(__file__).resolve().parents[2],
                    env=env, capture_output=True, text=True)
                (item / "predefines.log").write_text(proc.stdout + "\n" + proc.stderr)
                if proc.returncode or not macro_path.is_file():
                    raise RuntimeError("reference-GCC-macro-context-unavailable")
                predefs[context] = macro_path
            macro_path = predefs[context]
            common = ["-E", "-P", "-undef", "-x", "c", "-include", str(macro_path),
                      *pp_args(row["args"]), row["source"]]
            reference = item / "gnu.i"
            mini = item / "mini.i"
            run(["riscv64-linux-gnu-gcc", *common, "-o", str(reference)],
                out, item / "reference.log")
            run([str(args.minipp.resolve()), *common, "-o", str(mini)],
                out, item / "minipp.log")
            result["gnu_pp_sha"] = sha(reference)
            result["mini_pp_sha"] = sha(mini)
            if reference.read_bytes() != mini.read_bytes():
                # Fail closed and retain bounded first-difference context.
                import difflib
                diff = list(difflib.unified_diff(
                    reference.read_text(errors="replace").splitlines(),
                    mini.read_text(errors="replace").splitlines(),
                    fromfile="GNU", tofile="MiniPP", n=2))
                (item / "mismatch.diff").write_text("\n".join(diff[:100]) + "\n")
                raise RuntimeError("MiniPP-and-GNU-preprocessed-bytes-differ")
            asm = item / "minic.s"
            obj = item / "minias.o"
            run([str(args.minic_cc.resolve()), "-S", str(mini), "-o", str(asm)],
                out, item / "minic.log")
            march = next((x for x in row["args"] if x.startswith("-march=")),
                         "-march=rv64imac")
            abi = next((x for x in row["args"] if x.startswith("-mabi=")),
                       "-mabi=lp64")
            run([str(args.minias.resolve()), march, abi, "-o", str(obj), str(asm)],
                out, item / "minias.log")
            elf = obj.read_bytes()
            golden = (out / object_name).read_bytes()
            if not (len(elf) >= 52 and elf[:6] == b"\x7fELF\x02\x01"
                    and int.from_bytes(elf[16:18], "little") == 1
                    and int.from_bytes(elf[18:20], "little") == 243
                    and elf[48:52] == golden[48:52]):
                raise RuntimeError("MiniPP-MiniC-MiniAS-ELF64-RISC-V-ABI-mismatch")
            if sha(out / object_name) != old_sha:
                raise RuntimeError("golden-cached-object-overwritten")
            result.update(status="PASS", native_object_sha=sha(obj),
                          native_object_bytes=obj.stat().st_size)
            print(f"MINIPP_SEVEN_NATIVE_OBJECT=PASS worker={args.shard}/7 "
                  f"object={object_name} pp=MiniPP cc=MiniC as=MiniAS "
                  "GNU_pp_consumed_by_MiniC=false", flush=True)
        except Exception as exc:
            result["error"] = str(exc)
            print(f"MINIPP_SEVEN_NATIVE_OBJECT=FAIL worker={args.shard}/7 "
                  f"object={object_name} reason={exc}", flush=True)
    (stage / "results.json").write_text(json.dumps(rows, indent=2, sort_keys=True) + "\n")
    passed = sum(x["status"] == "PASS" for x in rows)
    (stage / "summary.json").write_text(json.dumps({
        "shard": args.shard, "shards": 7, "selected": len(rows),
        "passed": passed, "failed": len(rows)-passed, "full_universe": len(owner_names),
        "status": "PASS" if passed == len(rows) else "FAIL",
        "qemu_claim": False, "full_2064_pp_claim": False,
    }, indent=2, sort_keys=True) + "\n")
    print(f"MINIPP_SEVEN_NATIVE_RESULT={'PASS' if passed == len(rows) else 'FAIL'} "
          f"worker={args.shard}/7 passed={passed}/{len(rows)} "
          "full_Kbuild_claim=false QEMU_claim=false", flush=True)
    return 0 if passed == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
