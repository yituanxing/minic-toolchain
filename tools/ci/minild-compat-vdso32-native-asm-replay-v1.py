#!/usr/bin/env python3
"""Replay the real Linux compat-vDSO ELF32 linker with four MiniAS objects.

No 2064 C owners, Kbuild relink or QEMU. GNU preprocesses four frozen .S
files as a diagnostic oracle; previous native-S full tests independently
prove that MiniPP's preprocessed .S bytes match this exact GNU -E -P
output. This replay changes ONLY the assembler vs the frozen four .o.
All objects, link argv, outputs, errors and wall time are retained.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time

NAMES = ("rt_sigreturn", "getcpu", "flush_icache", "note")
REL = Path("arch/riscv/kernel/compat_vdso")

def digest(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()

def run(argv: list[str], cwd: Path, logfile: Path) -> int:
    started = time.monotonic_ns()
    p = subprocess.run(argv, cwd=cwd, capture_output=True, text=True,
                       errors="replace", timeout=90)
    elapsed_ms = (time.monotonic_ns() - started) // 1_000_000
    logfile.write_text(
        "COMMAND " + json.dumps(argv) + "\n" +
        f"rc={p.returncode} elapsed_ms={elapsed_ms}\n" +
        "stdout:\n" + p.stdout[-10000:] +
        "\nstderr:\n" + p.stderr[-20000:] + "\n"
    )
    return p.returncode

def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--minias", type=Path, required=True)
    p.add_argument("--minild", type=Path, required=True)
    p.add_argument("--evidence", type=Path, required=True)
    a = p.parse_args()
    out = a.out.resolve()
    ev = a.evidence.resolve()
    ev.mkdir(parents=True, exist_ok=True)
    native = ev / "native-S-inputs"
    native.mkdir(exist_ok=True)
    results = []
    original = []
    native_objs = []
    for name in NAMES:
        rel = REL / (name + ".o")
        cmd = out / REL / ("." + name + ".o.cmd")
        ref = out / rel
        if not cmd.is_file() or not ref.is_file():
            raise ValueError(f"missing exact frozen .S command: {cmd} or {ref}")
        lines = cmd.read_text(errors="replace").splitlines()
        commands = [line.split(":=", 1)[1].strip() for line in lines
                    if re.match(r"^(?:savedcmd|cmd)_[^\n]+\s*:=", line)]
        if len(commands) != 1:
            raise ValueError(f"unsafe .S command cardinality for {name}: {len(commands)}")
        argv = shlex.split(commands[0])
        if Path(argv[0]).name != "riscv64-linux-gnu-gcc":
            raise ValueError(f"unexpected producer for {rel}: {argv[0]}")
        if argv.count("-c") != 1:
            raise ValueError(f"missing exact compile -c for {rel}")
        output_indices = [i for i, item in enumerate(argv[:-1]) if item == "-o"]
        if len(output_indices) != 1 or argv[output_indices[0]+1] != rel.as_posix():
            raise ValueError(f"non-canonical .S -o for {rel}")
        if any(x in (";", "&&", "||", "|", ">", "<") for x in argv):
            raise ValueError("shell operators are forbidden in frozen arguments")
        march = [x for x in argv if x.startswith("-march=")]
        abi = [x for x in argv if x.startswith("-mabi=")]
        if not march or not abi:
            raise ValueError("missing recorded RISC-V target ABI")
        assembly = native / (name + ".s")
        object_path = native / (name + ".o")
        pp_argv = ["/usr/bin/riscv64-linux-gnu-gcc", "-P", *argv[1:]]
        pp_argv[pp_argv.index("-c")] = "-E"
        pp_argv[output_indices[0]+2] = str(assembly)
        if run(pp_argv, out, ev / f"native-S-{name}-cpp.txt") != 0:
            raise RuntimeError(f"GNU diagnostic assembler-with-cpp failed: {name}")
        asm_argv = [str(a.minias.resolve()), march[-1], abi[-1],
                    "-o", str(object_path), str(assembly)]
        if run(asm_argv, out, ev / f"native-S-{name}-minias.txt") != 0:
            raise RuntimeError(f"MiniAS cannot assemble real .S: {name}")
        readelf = subprocess.run(
            ["riscv64-linux-gnu-readelf", "-Wh", "-SW", "-Wr", "-Ws",
             str(object_path)],
            cwd=out, capture_output=True, text=True, timeout=30)
        (ev / f"native-S-{name}-elf.txt").write_text(readelf.stdout + readelf.stderr)
        if readelf.returncode != 0 or "Class:                             ELF32" not in readelf.stdout:
            raise RuntimeError(f"MiniAS output is not ELF32: {name}")
        results.append({
            "owner": rel.as_posix(), "frozen_sha256": digest(ref),
            "native_sha256": digest(object_path), "native_bytes": object_path.stat().st_size,
            "preprocessed_sha256": digest(assembly), "march": march[-1],
            "mabi": abi[-1],
        })
        original.append(ref)
        native_objs.append(object_path)
    (ev / "native-S-objects.json").write_text(json.dumps(results, indent=2) + "\n")
    linker_args = [
        "-melf64lriscv", "-z", "noexecstack", "--no-warn-rwx-segments",
        "-shared", "-S", "-soname=linux-compat_vdso.so.1",
        "--build-id=sha1", "--hash-style=both", "--eh-frame-hdr",
        "-melf32lriscv", "-T", str(out / REL / "compat_vdso.lds"),
        *(str(p) for p in native_objs),
        "-o", str(ev / "native-S-compat-vdso.so")
    ]
    outcome = run([str(a.minild.resolve()), *linker_args], out,
                  ev / "native-S-link.txt")
    sys.stdout.write("MINILD_VDSO32_NATIVE_S_REPLAY="
                     + ("PASS" if outcome == 0 else "FAIL")
                     + f" objects=4 linker_rc={outcome}"
                     + " C_recompiled=0 four_inputs_produced_by_MiniAS=true"
                     + " GNU_PP_reference_only=true\n")
    for row in results:
        print("MINILD_VDSO32_NATIVE_S_OBJECT "
              + " ".join(f"{key}={value}" for key, value in row.items()))
    if outcome != 0:
        print((ev / "native-S-link.txt").read_text()[-5000:])
        return 2
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print("MINILD_VDSO32_NATIVE_S_REPLAY=INCONCLUSIVE "
              + str(exc), file=sys.stderr)
        sys.exit(3)
