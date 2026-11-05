#!/usr/bin/env python3
"""Strict no-cross-toolchain GNU -S A/B and real MachineIR body-call gate.

--mode golden: old direct-backend golden/differential coverage, currently green.
--mode production: additionally preload an ELF call-stack witness and prove
    GNU output ran *through* minic_machine_gnu_asm_emit_body.  This does not
    trust stdout, diagnostics, a linked-but-unused symbol or a .s marker.

Production tracing requires a Linux ELF *host* and an unstripped MiniC.
Neither ptrace permissions nor target AS/QEMU are used. Intentionally opt-in: the
targets' complete Core->MIR selectors must exist before this test turns green.
"""

import argparse
import difflib
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[3]
CASES = ("arith_call_frame", "cfg_global", "entry_backedge", "inline_positive")
TARGETS = ("aarch64", "loongarch64")
FIVE = ("riscv32", "riscv64", "x86_64", "aarch64", "loongarch64")
TRACED_FUNCTION = "minic_machine_gnu_asm_emit_body"


class Failure(Exception):
    pass


def run(args, *, cwd=None, env=None):
    proc = subprocess.run(args, cwd=cwd, env=env, text=True, capture_output=True)
    return proc


def demand(condition, message):
    if not condition:
        raise Failure(message)


def read_goldens():
    hashes = {}
    for raw in (Path(__file__).with_name("golden.sha256")).read_text().splitlines():
        if raw and not raw.startswith("#"):
            digest, case = raw.split()
            hashes[case] = digest
    return hashes


def check_label_integrity(asm, key):
    """Do not let a golden update hide a dangling Core CFG/source label."""
    names = set(re.findall(r"(?m)^(\.L(?:mini_\w+|minic_mir_\w+|user_\d+)):", asm))
    references = set(re.findall(r"\.L(?:mini_\w+|minic_mir_\w+|user_\d+)", asm))
    demand(references <= names, f"{key}: dangling branch/source labels: {sorted(references - names)}")


def check_functional_shapes(asm, arch, case):
    key = f"{arch}/{case}"
    if case == "arith_call_frame":
        for symbol in ("mir_callee:", "mir_frame:", "main:"):
            demand(symbol in asm, f"{key}: missing {symbol}")
        demand(bool(re.search(r"(?m)^\s*(?:bl|call)\s+mir_callee(?:@PLT)?\s*$", asm)),
               f"{key}: call to mir_callee missing")
        if arch == "aarch64":
            demand("x29" in asm and "sp" in asm, f"{key}: AAPCS64 frame absent")
        elif arch == "loongarch64":
            demand("$sp" in asm and "$ra" in asm, f"{key}: LA64 frame absent")
    elif case == "cfg_global":
        for marker in ("mir_cfg:", "mir_table:", "mir_global_ptr:"):
            demand(marker in asm, f"{key}: global/relocation missing {marker}")
        demand(re.search(r"(?m)^\s*\.(?:8byte|4byte|quad|long|dword|word)\s+mir_table\b", asm),
               f"{key}: global relocation to mir_table absent")
        demand(len(re.findall(r"(?m)^\.L\w+:", asm)) >= 5,
               f"{key}: target CFG labels unexpectedly absent")
    elif case == "entry_backedge":
        demand("mir_backedge:" in asm, f"{key}: missing loop function")
        demand(len(re.findall(r"(?m)^\.L\w+:", asm)) >= 2,
               f"{key}: loop labels unexpectedly absent")
    else:
        demand("mir_inline:" in asm, f"{key}: missing mir_inline")
        demand(len(re.findall(r"(?m)^\s*nop\s*$", asm)) == 1,
               f"{key}: source-owned inline ASM must appear exactly once")
    check_label_integrity(asm, key)


def symbol_vaddr(binary, name):
    proc = run(["nm", "-anS", str(binary)])
    demand(proc.returncode == 0, f"nm failed on {binary}: {proc.stderr}")
    hits = [(int(parts[0], 16), int(parts[1], 16))
            for line in proc.stdout.splitlines()
            if (parts := line.split()) and len(parts) == 4
            and parts[3] == name and parts[2].upper() == "T"]
    demand(len(hits) == 1, f"{binary}: expected exactly one unstripped {name}, got {len(hits)}")
    with binary.open("rb") as handle:
        header = handle.read(20)
    demand(header[:4] == b"\x7fELF", f"{binary}: cannot trace non-ELF binary")
    endian = "little" if header[5] == 1 else "big"
    elf_type = int.from_bytes(header[16:18], endian)
    demand(elf_type in (2, 3), f"{binary}: unsupported ELF type {elf_type}")
    return hits[0]


def build_witness(host_cc, work):
    shared = work / "mir-call-witness.so"
    proc = run([host_cc, "-std=c11", "-O0", "-g", "-shared", "-fPIC",
                "-Wall", "-Wextra", "-Werror", str(Path(__file__).with_name(
                    "mir_call_witness.c")), "-ldl", "-o", str(shared)])
    demand(proc.returncode == 0, f"cannot build host MIR call witness: {proc.stderr}")
    return shared


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("golden", "production"), required=True)
    parser.add_argument("--targets", choices=("a64-loong", "five"),
                        default="a64-loong")
    parser.add_argument("--minic-cc", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/debug")
    parser.add_argument("--host-cc", default="cc")
    parser.add_argument("--baseline-cc", type=Path,
                        help="optional old compiler: generate and show unified diff on mismatch")
    args = parser.parse_args()
    binary = args.minic_cc.resolve(strict=True)
    baseline = args.baseline_cc.resolve(strict=True) if args.baseline_cc else None
    work = args.build_dir.resolve() / "tests/mir-migration"
    work.mkdir(parents=True, exist_ok=True)
    golden = read_goldens()
    archs = FIVE if args.targets == "five" else TARGETS
    demand(not (args.mode == "production" and args.targets == "five"),
           "MIR production gate currently covers only AArch64 and LoongArch64")
    witness = None
    function_start = None
    function_size = None
    if args.mode == "production":
        function_start, function_size = symbol_vaddr(binary, TRACED_FUNCTION)
        witness = build_witness(args.host_cc, work)
    calls = 0
    positives = 0
    for case in CASES:
        source = Path(__file__).with_name(case + ".c")
        preprocessed = work / (case + ".i")
        proc = run([args.host_cc, "-E", "-P", "-std=gnu11", "-x", "c",
                    str(source), "-o", str(preprocessed)])
        demand(proc.returncode == 0, f"GNU preprocess {case}: {proc.stderr}")
        for arch in archs:
            # x86 direct codegen has not admitted this source ASM shape.
            if arch == "x86_64" and case == "inline_positive":
                continue
            key = f"{arch}/{case}"
            output = work / f"{arch}-{case}.s"
            command = [f"--target={arch}-linux", "-S", str(preprocessed),
                       "-o", str(output)]
            env = None
            marker = work / f"witness-{arch}-{case}.txt"
            if args.mode == "production":
                marker.unlink(missing_ok=True)
                env = os.environ.copy()
                env.update({"LD_PRELOAD": str(witness),
                            "MINIC_MIR_WITNESS_START": hex(function_start),
                            "MINIC_MIR_WITNESS_SIZE": hex(function_size),
                            "MINIC_MIR_WITNESS_PATH": str(marker),
                            "MINIC_MIR_OUTPUT_PATH": str(output)})
            proc = run([str(binary)] + command, env=env)
            demand(proc.returncode == 0 and output.is_file(),
                   f"{key}: MiniC failed ({proc.returncode}): {proc.stderr}")
            if args.mode == "production":
                demand(marker.is_file() and marker.read_text() ==
                       "typed-MIR-serializer-call\n",
                       f"{key}: production used old direct FILE* path; "
                       f"live stack never entered {TRACED_FUNCTION}")
                calls += 1
            payload = output.read_bytes()
            demand(bool(payload), f"{key}: empty .s")
            asm = payload.decode("utf-8")
            check_functional_shapes(asm, arch, case)
            digest = hashlib.sha256(payload).hexdigest()
            demand(key in golden, f"{key}: missing committed baseline golden")
            if digest != golden[key]:
                detail = ""
                if baseline is not None:
                    old = work / f"baseline-{arch}-{case}.s"
                    old_run = run([str(baseline), f"--target={arch}-linux", "-S",
                                   str(preprocessed), "-o", str(old)])
                    demand(old_run.returncode == 0, f"old compiler failed: {old_run.stderr}")
                    delta = difflib.unified_diff(old.read_text().splitlines(),
                                                 asm.splitlines(), fromfile="baseline",
                                                 tofile="current", lineterm="")
                    detail = "\n" + "\n".join(list(delta)[:60])
                raise Failure(f"{key}: golden divergence expected={golden[key]} "
                              f"actual={digest}, output={output}{detail}")
            print(f"MIR_AB_GOLDEN=PASS target={arch} case={case} bytes={len(payload)} sha256={digest}")
            positives += 1

    negative = Path(__file__).with_name("inline_negative.c")
    neg_i = work / "inline_negative.i"
    proc = run([args.host_cc, "-E", "-P", "-std=gnu11", "-x", "c",
                str(negative), "-o", str(neg_i)])
    demand(proc.returncode == 0, f"GNU preprocess negative: {proc.stderr}")
    for arch in TARGETS:
        output = work / f"negative-{arch}.s"
        output.unlink(missing_ok=True)
        proc = run([str(binary), f"--target={arch}-linux", "-S", str(neg_i),
                    "-o", str(output)])
        demand(proc.returncode != 0, f"{arch}: missing operand in source ASM admitted")
        demand(not output.exists(), f"{arch}: negative source ASM emitted partial .s")
        demand("error:" in proc.stderr and "Segmentation fault" not in proc.stderr,
               f"{arch}: bad negative diagnostic: {proc.stderr}")
        print(f"MIR_ASM_NEGATIVE=PASS target={arch} rejected=unbound_operand")
    print(f"MIR_MIGRATION={args.mode.upper()}_PASS targets={len(archs)} positive={positives} negative=2 "
          f"typed_emitter_calls={calls}")


if __name__ == "__main__":
    try:
        main()
    except (Failure, OSError, UnicodeError) as error:
        print(f"MIR_MIGRATION=FAIL {error}", file=sys.stderr)
        sys.exit(1)
