#!/usr/bin/env python3
"""Lock one GCC-preprocessed Linux TU; vary MiniC, not the GNU toolchain.

Run --mode establish once to compile the same frozen .i through GNU GCC
and MiniC (both assembled with the same GNU assembler driver). On subsequent
--mode iterate invocations verify the pinned GCC .o, .i, configuration,
flags and GNU compiler identity, then rebuild *only* MiniC's .s/.o.

This is a single-TU provenance/build primitive, NOT a Linux boot verdict.
The caller must use a certified GCC kernel baseline, isolated object overlays,
relink and QEMU with their own PASS/FAIL/INCONCLUSIVE oracle.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile

SCHEMA = "minic-linux-gnu-locked-single-tu-v1"


class InputError(RuntimeError):
    pass


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as src:
        for data in iter(lambda: src.read(1024 * 1024), b""):
            h.update(data)
    return h.hexdigest()


def flags(path: Path) -> list[str]:
    # Each non-comment line denotes exactly ONE argument; never eval a shell.
    if not path.is_file():
        raise InputError(f"missing flags file: {path}")
    result = [s for raw in path.read_text().splitlines()
              if (s := raw.strip()) and not s.startswith("#")]
    if any(s in {"-o", "-S", "-c", "-E", "-x"} or
           s.startswith("-o") or s.startswith("@") for s in result):
        raise InputError("flags cannot override compilation stage, output or response file")
    return result


def resolved_exe(raw: str) -> Path:
    exe = shutil.which(raw)
    if not exe:
        raise InputError(f"tool not found: {raw}")
    result = Path(exe).resolve()
    if not result.is_file():
        raise InputError(f"not a regular executable: {raw}")
    return result


def elf_rv64(path: Path) -> int:
    if not path.is_file():
        raise InputError(f"missing ELF object: {path}")
    with path.open("rb") as src:
        hdr = src.read(64)
    if len(hdr) < 52 or hdr[:6] != b"\x7fELF\x02\x01":
        raise InputError(f"not a little-endian ELF64 object: {path}")
    if struct.unpack_from("<H", hdr, 16)[0] != 1:
        raise InputError(f"not an ET_REL object: {path}")
    if struct.unpack_from("<H", hdr, 18)[0] != 243:
        raise InputError(f"not a RISC-V object: {path}")
    return struct.unpack_from("<I", hdr, 48)[0]


def atomic_json(path: Path, obj: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                     prefix=".temp-", delete=False) as tmp:
        temp = Path(tmp.name)
        json.dump(obj, tmp, sort_keys=True, indent=2)
        tmp.write("\n")
    try:
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def run(cmd: list[str], *, log: Path) -> None:
    log.parent.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write_bytes(proc.stdout)
    if proc.returncode:
        raise InputError(f"command failed rc={proc.returncode}: {shlex.join(cmd)}; log={log}")


def identity(args: argparse.Namespace, gcc: Path) -> dict:
    input_file, config = args.input.resolve(), args.config.resolve()
    if input_file.suffix != ".i" or not input_file.is_file():
        raise InputError(f"expected frozen .i input: {input_file}")
    if not config.is_file():
        raise InputError(f"missing .config: {config}")
    path = PurePosixPath(args.object)
    if path.is_absolute() or ".." in path.parts or not str(path).endswith(".o"):
        raise InputError(f"unsafe target object: {args.object!r}")
    return {
        "schema": SCHEMA,
        "object": str(path),
        "input_sha256": sha(input_file),
        "config_sha256": sha(config),
        "gcc_flags_sha256": sha(args.gcc_flags),
        "gnu_as_flags_sha256": sha(args.as_flags),
        "gnu_driver_sha256": sha(gcc),
        "gnu_driver_path": str(gcc),
    }


def build(args: argparse.Namespace) -> dict:
    gcc = resolved_exe(args.gnu_cc)
    minic = resolved_exe(args.minic)
    minic_binary_sha = sha(minic)
    if gcc == minic:
        raise InputError("MiniC and GNU GCC driver must be distinct executables")
    work = args.work.resolve()
    baseline = work / "baseline.json"
    gcc_s, gcc_o = work / "gnu" / "reference.s", work / "gnu" / "reference.o"
    mini_s, mini_o = work / "minic" / "candidate.s", work / "minic" / "candidate.o"
    current = identity(args, gcc)
    gcc_args, asm_args = flags(args.gcc_flags), flags(args.as_flags)
    # Two important paths must not be inside mutable experiment output.
    if args.input.resolve().is_relative_to(work) or args.config.resolve().is_relative_to(work):
        raise InputError("input and config must be outside the mutable work directory")
    work.mkdir(parents=True, exist_ok=True)
    if args.mode == "establish":
        if baseline.exists():
            raise InputError("baseline exists; use --mode iterate or start a NEW work directory")
        gcc_s.parent.mkdir(parents=True, exist_ok=True)
        gcc_o.unlink(missing_ok=True)
        gcc_s.unlink(missing_ok=True)
        run([str(gcc), *gcc_args, "-x", "cpp-output", "-S", str(args.input.resolve()), "-o", str(gcc_s)],
            log=work / "gnu" / "compile.log")
        run([str(gcc), *asm_args, "-x", "assembler", "-c", str(gcc_s), "-o", str(gcc_o)],
            log=work / "gnu" / "assemble.log")
        current["gnu_object_elf_flags"] = elf_rv64(gcc_o)
        current["gnu_assembly_sha256"] = sha(gcc_s)
        current["gnu_object_sha256"] = sha(gcc_o)
        if current["input_sha256"] != sha(args.input):
            raise InputError("frozen .i changed during GCC baseline build")
        atomic_json(baseline, current)
    else:
        if not baseline.is_file():
            raise InputError("no pinned GCC baseline; establish it first")
        expected = json.loads(baseline.read_text())
        # Prove all inputs/tool identities are identical, and the GCC object is unchanged.
        for key, value in current.items():
            if expected.get(key) != value:
                raise InputError(f"GCC baseline mismatch: {key}")
        if not gcc_s.is_file() or sha(gcc_s) != expected.get("gnu_assembly_sha256"):
            raise InputError("GCC assembly was changed since baseline certification")
        if elf_rv64(gcc_o) != expected.get("gnu_object_elf_flags") or sha(gcc_o) != expected.get("gnu_object_sha256"):
            raise InputError("GCC reference object was changed since baseline certification")
        current = expected

    # Never allow stale MiniC results to survive a failed iteration.
    (work / "last-experiment.json").unlink(missing_ok=True)
    mini_s.parent.mkdir(parents=True, exist_ok=True)
    mini_s.unlink(missing_ok=True)
    mini_o.unlink(missing_ok=True)
    run([str(minic), "-S", str(args.input.resolve()), "-o", str(mini_s)],
        log=work / "minic" / "compile.log")
    run([str(gcc), *asm_args, "-x", "assembler", "-c", str(mini_s), "-o", str(mini_o)],
        log=work / "minic" / "assemble.log")
    candidate_flags = elf_rv64(mini_o)
    if candidate_flags != current["gnu_object_elf_flags"]:
        mini_o.unlink(missing_ok=True)
        raise InputError(f"GNU-assembled candidate ABI flags differ from GCC object: {candidate_flags:#x} != {current['gnu_object_elf_flags']:#x}")
    if sha(minic) != minic_binary_sha or sha(gcc) != current["gnu_driver_sha256"]:
        mini_o.unlink(missing_ok=True)
        raise InputError("tool executable changed during trial")
    if sha(args.input) != current["input_sha256"] or sha(args.config) != current["config_sha256"]:
        mini_o.unlink(missing_ok=True)
        raise InputError("frozen input/config changed during candidate build")

    report = {
        "schema": SCHEMA,
        "mode": args.mode,
        "reference": current,
        "minic_binary_sha256": minic_binary_sha,
        "minic_assembly_sha256": sha(mini_s),
        "minic_object_sha256": sha(mini_o),
        "gnu_object": str(gcc_o),
        "minic_object": str(mini_o),
        "verdict": "OBJECTS_BUILT_NOT_RUNTIME_CERTIFIED",
    }
    atomic_json(work / "last-experiment.json", report)
    return report


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mode", choices=["establish", "iterate"], required=True)
    p.add_argument("--object", required=True, help="kernel-relative .o path, for provenance")
    p.add_argument("--input", type=Path, required=True, help="frozen GCC-preprocessed .i")
    p.add_argument("--config", type=Path, required=True, help="exact Linux .config of this TU")
    p.add_argument("--gcc-flags", type=Path, required=True, help="literal one-argument-per-line GCC C compilation options")
    p.add_argument("--as-flags", type=Path, required=True, help="literal one-argument-per-line GNU assembler driver options")
    p.add_argument("--gnu-cc", default="riscv64-linux-gnu-gcc")
    p.add_argument("--minic", required=True)
    p.add_argument("--work", type=Path, required=True)
    return p


def main(argv=None) -> int:
    args = parser().parse_args(argv)
    try:
        result = build(args)
    except (InputError, OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"GNU_LOCKED_SINGLE_TU=ERROR reason={exc}", file=sys.stderr)
        return 2
    print(f"GNU_LOCKED_SINGLE_TU=PASS mode={args.mode} object={args.object} input_sha={result['reference']['input_sha256']} gcc_o={result['reference']['gnu_object_sha256']} minic_o={result['minic_object_sha256']} verdict=OBJECTS_BUILT_NOT_RUNTIME_CERTIFIED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
