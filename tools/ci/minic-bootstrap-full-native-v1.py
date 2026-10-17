#!/usr/bin/env python3
"""Fail-closed, all-native toolchain self-rebuild frontier.

Stage2 MiniC + Stage2-built eight RV64 tools are immutable seeds.
Every *new* C -> .i -> .s -> .o -> ELF operation uses only MiniPP,
MiniC, MiniAS and MiniLD through qemu-riscv64. No GNU compiler,
preprocessor, assembler, linker, or objcopy subprocess is allowed.
Prebuilt target libc/CRT/libgcc are disclosed as independent runtime
inputs, NOT counted as natively rebuilt. A passing native fixed point
requires a complete Stage3 -> Stage4 rebuild of all nine binaries.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
STAGE2_SHA = "17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d"
CFLAGS = dict(MINIC_LINUX_FULL_CROSSBLOCK="1",
              MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ="1")
INCLUDES = ("include", "src", "elf/include", "archiver/include",
            "assembler/src", "preprocessor/include", "preprocessor/src",
            "preprocessor/target", "linker/include", "linker/src")
NATIVE = ("minic-cc", "minic-cpp", "minic-as", "minic-ar", "minic-ld",
          "minic-nm", "minic-strip", "minic-objcopy", "minic")
RV64_FLAGS = ("-std=gnu11", "-fshort-wchar", "-funsigned-char", "-fno-PIE",
              "-mabi=lp64", "-march=rv64imac_zicsr_zifencei_zihintpause",
              "-mcmodel=medany")


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def execute(argv, log, *, timeout=300, env=None, expected=0):
    """Explicit allowlist: never spawn GCC, GNU AS/LD or objcopy."""
    args = [str(x) for x in argv]
    first = Path(args[0]).name
    if first != "qemu-riscv64":
        raise RuntimeError("FORBIDDEN_EXEC " + " ".join(args[:3]))
    start = time.monotonic()
    try:
        p = subprocess.run(args, cwd=ROOT, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        raise RuntimeError("TIMEOUT " + " ".join(args[:4])) from None
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_bytes(p.stdout + b"\n-- stderr --\n" + p.stderr)
    if expected is not None and p.returncode != expected:
        raise RuntimeError("EXIT_%s expected=%s command=%s stderr=%s" %
                           (p.returncode, expected, " ".join(args[:5]),
                            p.stderr[-1100:].decode(errors="replace")))
    return round(time.monotonic() - start, 3)


def inventory():
    path = ROOT / "tools/ci/minic-bootstrap-stage2-toolchain-v1.py"
    spec = importlib.util.spec_from_file_location("mini_source_plan", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    ccs = module.source_group((ROOT / "Makefile").read_text(), "MINIC_SOURCES")
    manifest = (ROOT / "tests/compiler/bootstrap/b0-sources.txt").read_text().splitlines()
    if ccs != manifest or len(ccs) != 47:
        raise RuntimeError("compiler source manifest is not current 47/47")
    plan, others = module.source_plan(module.CANONICAL)
    source_lists = {"minic-cc": ccs}
    source_lists.update({tool: plan["#" + tool]["sources"] for tool in module.CANONICAL})
    all_sources = list(dict.fromkeys(ccs + others))
    if len(source_lists) != 9 or len(all_sources) < 70:
        raise RuntimeError("incomplete 9-tool source inventory")
    for src in all_sources:
        if not (ROOT / src).is_file():
            raise RuntimeError("missing production source " + src)
    return source_lists, all_sources


def verify_seed(stage2, artifact):
    if sha(stage2) != STAGE2_SHA:
        raise RuntimeError("uncertified Stage2 compiler")
    report = json.loads((artifact / "summary.json").read_text())
    if report["stage2_sha256"] != STAGE2_SHA or len(report["tus"]) != 30:
        raise RuntimeError("wrong Stage2-built toolchain manifest")
    if set(report["binaries"]) != set(NATIVE) - {"minic-cc"}:
        raise RuntimeError("not the certified eight-tool build")
    binaries = {"minic-cc": stage2}
    for name in NATIVE:
        if name == "minic-cc":
            continue
        elf = artifact / "bin" / name
        if not elf.is_file() or sha(elf) != report["binaries"][name]["sha256"]:
            raise RuntimeError("Stage2-built tool checksum mismatch " + name)
        elf.chmod(elf.stat().st_mode | 0o111)
        binaries[name] = elf
    stage2.chmod(stage2.stat().st_mode | 0o111)
    return binaries


def prebuilt_runtime():
    """Only existing binary runtime inputs; never invoke a host compiler."""
    base = Path("/usr/riscv64-linux-gnu")
    hdr = base / "include"
    lib = base / "lib"
    gcc_roots = sorted(Path("/usr/lib/gcc-cross/riscv64-linux-gnu").glob("*/libgcc.a"))
    required = {n: lib / n for n in ("crt1.o", "crti.o", "crtn.o", "libc.a")}
    if not hdr.is_dir() or not gcc_roots or not all(p.is_file() for p in required.values()):
        raise RuntimeError("MISSING_PREBUILT_RUNTIME headers/crt/libc/libgcc")
    required["libgcc.a"] = gcc_roots[-1]
    return hdr, required


def command(binaries, tool, *args):
    return ["qemu-riscv64", binaries[tool], *args]


def native_pp(binaries, src, target, macros, hdr, logs, env):
    argv = command(binaries, "minic-cpp", "-E", "-P", "-undef", "-nostdinc",
                   "-DNDEBUG", "-D__riscv_float_abi_double=1",
                   "-D__riscv_flen=64",
                   *["-I" + str(ROOT / x) for x in INCLUDES],
                   "-I" + str(hdr), "-include", macros, "-o", target, ROOT / src)
    return execute(argv, logs, timeout=180, env=env)


def native_object(binaries, inp, asm, obj, log, env):
    compile_seconds = execute(command(binaries, "minic-cc", "-S", inp, "-o", asm),
                              log.with_suffix(".cc.log"), timeout=1100, env=env)
    as_seconds = execute(command(binaries, "minic-as", "-o", obj, asm),
                         log.with_suffix(".as.log"), timeout=240, env=env)
    if not asm.is_file() or not obj.is_file() or obj.stat().st_size < 64:
        raise RuntimeError("missing assembly/object " + str(inp))
    return compile_seconds, as_seconds


def source_key(source):
    return source.replace("/", "__").replace(".", "_")


def rounds(binaries, sources, all_sources, hdr, libraries, out, workers):
    env = dict(os.environ, **CFLAGS)
    for stage in (3, 4):
        name = "stage" + str(stage)
        work = out / name
        for dirname in ("i", "s", "o", "bin", "logs"):
            (work / dirname).mkdir(parents=True, exist_ok=True)
        prev = binaries
        macros = work / "target-predefines.h"
        flags = list(RV64_FLAGS)
        execute(command(prev, "minic-cpp", "--dump-target-predefines",
                        "--language", "c", "--output", macros, "--", *flags),
                work / "logs/target-predefines.log", env=env)
        if not macros.is_file() or macros.stat().st_size < 1000:
            raise RuntimeError(name + " native target predefines missing")
        # The pinned kernel profile is soft-float. These sources use the
        # lp64d cross-glibc sysroot. Overlay the ABI before any libc include.
        overlay = work / "target-lp64d-overlay.h"
        # The forced include is an absolute file path. MiniPP's nested quoted
        # include lookup is directory-relative, so put the pinned native macro
        # definitions DIRECTLY in this overlay rather than #include "/abs".
        overlay.write_text(macros.read_text() +
                           "\n#undef __riscv_float_abi_soft\n"
                           "#define __riscv_float_abi_double 1\n"
                           "#define __riscv_flen 64\n")
        completed = []
        failures = []
        def one(src):
            key = source_key(src)
            inp = out / "canonical-inputs" / (key + ".i")
            inp.parent.mkdir(parents=True, exist_ok=True)
            asm = work / "s" / (key + ".s")
            obj = work / "o" / (key + ".o")
            begin = time.monotonic()
            native_pp(prev, src, inp, overlay, hdr,
                      work / "logs" / (key + ".pp.log"), env)
            if not inp.is_file() or inp.stat().st_size == 0:
                raise RuntimeError("empty preprocessor output")
            cc, assembler = native_object(prev, inp, asm, obj,
                                          work / "logs" / key, env)
            return {"source": src, "pp_and_all_sec": round(time.monotonic()-begin, 3),
                    "cc_sec": cc, "as_sec": assembler,
                    "i": sha(inp), "s": sha(asm), "o": sha(obj)}
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = {pool.submit(one, s): s for s in all_sources}
            for future in as_completed(futures):
                src = futures[future]
                try:
                    row = future.result()
                    completed.append(row)
                    print("FULL_NATIVE_TU=PASS round=%s count=%d/%d source=%s sec=%.3f" %
                          (name, len(completed), len(all_sources), src,
                           row["pp_and_all_sec"]), flush=True)
                except Exception as exc:
                    failures.append({"source": src, "error": str(exc)})
                    print("FULL_NATIVE_TU=FAIL round=%s source=%s reason=%s" %
                          (name, src, str(exc)[:250]), flush=True)
        summary = {"round": name, "seed_binaries": {k: sha(v) for k,v in prev.items()},
                   "source_count": len(all_sources), "passed": len(completed),
                   "failed": failures, "rows": sorted(completed, key=lambda r: r["source"]),
                   "runtime_seed": {k: sha(v) for k,v in libraries.items()}}
        (work / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        if failures:
            raise RuntimeError("%s_PREPROCESS_COMPILE_ASSEMBLE blockers=%d first=%s" %
                               (name, len(failures), failures[0]))
        if stage == 4:
            earlier = json.loads((out / "stage3/summary.json").read_text())
            byname = {row["source"]: row for row in earlier["rows"]}
            mismatch = [r["source"] for r in completed if
                        r["i"] != byname[r["source"]]["i"] or
                        r["s"] != byname[r["source"]]["s"] or
                        r["o"] != byname[r["source"]]["o"]]
            if mismatch:
                raise RuntimeError("STAGE3_STAGE4_PER_TU_MISMATCH " + ",".join(mismatch[:10]))
            print("FULL_NATIVE_FIXEDPOINT_TUS=PASS count=%d" % len(completed), flush=True)
        next_binaries = {}
        for tool, units in sources.items():
            elf = work / "bin" / tool
            objs = [work / "o" / (source_key(s) + ".o") for s in units]
            link_args = command(prev, "minic-ld", "-melf64lriscv", "-static",
                                "-e", "_start", "-o", elf,
                                libraries["crt1.o"], libraries["crti.o"], *objs,
                                "--start-group", libraries["libc.a"],
                                libraries["libgcc.a"], "--end-group",
                                libraries["crtn.o"])
            execute(link_args, work / "logs" / (tool + ".ld.log"),
                    timeout=480, env=env)
            if not elf.is_file() or elf.stat().st_size < 1024:
                raise RuntimeError("missing linked native tool " + tool)
            elf.chmod(0o755)
            # A native binary must genuinely start under QEMU.
            execute(command({"run": elf}, "run", "--help"),
                    work / "logs" / (tool + ".help.log"), timeout=60, env=env,
                    expected=None)
            next_binaries[tool] = elf
            print("FULL_NATIVE_LINK=PASS round=%s tool=%s elf_sha256=%s" %
                  (name, tool, sha(elf)), flush=True)
        summary["linked"] = {tool: sha(p) for tool,p in next_binaries.items()}
        (work / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        if stage == 4:
            old = json.loads((out / "stage3/summary.json").read_text())["linked"]
            # Raw ELF can differ in non-loadable section/symbol metadata.
            # Stage4-built MiniObjcopy (not GNU objcopy) compares loadable bytes.
            for tool in NATIVE:
                image3 = work / "bin" / (tool + ".stage3.bin")
                image4 = work / "bin" / (tool + ".stage4.bin")
                execute(command(next_binaries, "minic-objcopy", "-O", "binary",
                                out / "stage3/bin" / tool, image3),
                        work / "logs" / (tool + ".stage3-objcopy.log"), env=env)
                execute(command(next_binaries, "minic-objcopy", "-O", "binary",
                                out / "stage4/bin" / tool, image4),
                        work / "logs" / (tool + ".stage4-objcopy.log"), env=env)
                if sha(image3) != sha(image4):
                    raise RuntimeError("STAGE3_STAGE4_LOADABLE_MISMATCH tool=" + tool)
            print("FULL_NATIVE_TOOLCHAIN=PASS tools=9 native_pp_cc_as_ld=true"
                  " prebuilt_libc_crt_libgcc=true stage3_stage4_loadable_exact=true"
                  " gcc_as_ld_invocations=0", flush=True)
        binaries = next_binaries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage2", type=Path, required=True)
    parser.add_argument("--toolchain", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--workers", type=int, choices=(1, 2, 4), default=4)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sources, all_sources = inventory()
    stage2 = args.stage2.resolve()
    artifact = args.toolchain.resolve()
    seed = verify_seed(stage2, artifact)
    hdr, libraries = prebuilt_runtime()
    print("FULL_NATIVE_SEED=PASS tools=9 source_tus=%d stage2_sha=%s"
          " external_runtime=prebuilt_cross_libc_crt_libgcc" %
          (len(all_sources), sha(stage2)), flush=True)
    rounds(seed, sources, all_sources, hdr, libraries, out, args.workers)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, KeyError, ValueError) as exc:
        print("FULL_NATIVE_TOOLCHAIN=BLOCKED " + str(exc), file=sys.stderr, flush=True)
        sys.exit(1)
