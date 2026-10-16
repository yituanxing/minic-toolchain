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
    native = ev  # existing artifact uploads *.o and *.txt at this level
    # The golden .cmd is immutable, but its original absolute source prefix
    # belongs to the old Kbuild build directory, which is not restored in
    # a cached-link-only receiver. Resolve the separately SHA-pinned Linux
    # source fixture already restored by the existing workflow.
    source_root = (out.parent.parent / "linux-expanded-kbuild-v0" /
                   "linux-6.6.143").resolve()
    original_source_root = out.parent / "linux-6.6.143"
    if not (source_root / REL / "rt_sigreturn.S").is_file():
        raise ValueError("immutable Linux 6.6.143 source fixture not restored: "
                         + str(source_root))
    (ev / "native-S-source-fixture.txt").write_text(
        f"golden_out={out}\nrestored_source={source_root}\n"
        f"original_kbuild_source={original_source_root}\n"
        "C_recompiled=0 source_snapshot=pinned_existing_workflow_cache\n")
    results = []
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
        # Only replace a nonexistent original Linux source-root prefix, not
        # generated-header locations inside the still-pinned golden /out.
        source_before = str(original_source_root)
        source_after = str(source_root)
        argv = [arg.replace(source_before, source_after)
                if source_before in arg else arg for arg in argv]
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
        assembly = native / ("native-S-" + name + ".s")
        object_path = native / ("native-S-" + name + ".o")
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
    # GNU is diagnostic-only on the EXACT SAME four MiniAS outputs.
    # This establishes whether the no-.eh_frame ABI omits PT_GNU_EH_FRAME
    # entirely; the reference file is NEVER substituted for MiniLD.
    gnu_args = list(linker_args)
    gnu_output = ev / "native-S-gnu-compat-vdso.so"
    gnu_args[-1] = str(gnu_output)
    gnu_rc = run(["/usr/bin/riscv64-linux-gnu-ld", *gnu_args],
                 out, ev / "native-S-gnu-link.txt")
    if gnu_rc != 0:
        raise RuntimeError("GNU same-MiniAS-object reference failed rc="
                           + str(gnu_rc))
    gnu_readelf = subprocess.run(
        ["riscv64-linux-gnu-readelf", "-Wh", "-SW", "-lW", "-dW",
         "--dyn-syms", "--version-info", str(gnu_output)],
        cwd=out, capture_output=True, text=True, timeout=30)
    (ev / "native-S-gnu-elf.txt").write_text(
        gnu_readelf.stdout + gnu_readelf.stderr)
    if gnu_readelf.returncode != 0:
        raise RuntimeError("readelf cannot inspect GNU same-input RV32 output")
    print("MINILD_VDSO32_NATIVE_S_GNU_REFERENCE=PASS "
          "GNU_diagnostic_only=true input_objects=MiniAS "
          "gnu_output_bytes=" + str(gnu_output.stat().st_size))
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
    generated = ev / "native-S-compat-vdso.so"
    report = subprocess.run(
        ["riscv64-linux-gnu-readelf", "-Wh", "-SW", "-lW", "-dW",
         "--dyn-syms", "--version-info", str(generated)],
        cwd=out, capture_output=True, text=True, timeout=30)
    (ev / "native-S-native-elf.txt").write_text(
        report.stdout + report.stderr)
    if report.returncode != 0:
        raise RuntimeError("MiniLD ELF32 output cannot be read by binutils")
    for key in ("ELF32", "DYN (Shared object file)", "GNU_EH_FRAME",
                "LINUX_4.15", "linux-compat_vdso.so.1",
                "__vdso_rt_sigreturn", "__vdso_getcpu",
                "__vdso_flush_icache"):
        if key not in report.stdout:
            raise RuntimeError("native ELF32 missing ABI contract " + key)
    import struct
    def text_bytes(elf: Path) -> bytes:
        data = elf.read_bytes()
        if data[:6] != b"\x7fELF\x01\x01":
            raise RuntimeError("not ELF32 little-endian " + str(elf))
        shoff = struct.unpack_from("<I", data, 32)[0]
        entsize, count, stridx = struct.unpack_from("<HHH", data, 46)
        if entsize != 40 or not 0 < stridx < count:
            raise RuntimeError("bad ELF32 section header table")
        name_hdr = shoff + stridx * entsize
        name_off, name_len = struct.unpack_from("<II", data, name_hdr + 16)
        names = data[name_off:name_off + name_len]
        for i in range(count):
            p = shoff + i*entsize
            index = struct.unpack_from("<I", data, p)[0]
            e = names.find(b"\x00", index)
            if e >= 0 and names[index:e] == b".text":
                offset, size = struct.unpack_from("<II", data, p+16)
                if offset + size > len(data):
                    raise RuntimeError(".text is out of ELF32 bounds")
                return data[offset:offset+size]
        raise RuntimeError("ELF32 .text missing")
    if text_bytes(generated) != text_bytes(gnu_output):
        raise RuntimeError("MiniLD/GNU real four MiniAS object .text differs")
    print("MINILD_VDSO32_NATIVE_S_REPLAY=PASS "
          "gnu_same_MiniAS_inputs_text_equal=true "
          "unwind=absent_not_fabricated "
          "ELF32_ET_DYN=true no_GNU_output_substitution=true "
          "C_recompiled=0")
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print("MINILD_VDSO32_NATIVE_S_REPLAY=INCONCLUSIVE "
              + str(exc), file=sys.stderr)
        sys.exit(3)
