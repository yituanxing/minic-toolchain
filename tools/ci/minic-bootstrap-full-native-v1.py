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
import shutil
import shlex
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
        raise RuntimeError("EXIT_%s expected=%s stderr=%s command=%s" %
                           (p.returncode, expected,
                            p.stderr[-1100:].decode(errors="replace"),
                            " ".join(args[:5])))
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
    # GCC builtin headers are currently an external seed input, not executables.
    # Keep this provenance visible; replacing them with native freestanding
    # headers is a separate condition for fully GCC-independent bootstrap.
    builtin = gcc_roots[-1].parent / "include"
    if not builtin.is_dir():
        raise RuntimeError("MISSING_TARGET_BUILTIN_HEADERS")
    return (builtin, hdr), required


def musl_runtime(build, original):
    """Audited binary producer; this seed must have disclosed provenance."""
    b = Path(build).resolve()
    p = {"crt1.o": b / "obj/crt/crt1.o",
         "crti.o": b / "obj/crt/crti.o",
         "crtn.o": b / "obj/crt/crtn.o",
         "libc.a": b / "lib/libc.a",
         "libgcc.a": original["libgcc.a"]}
    for name, path in p.items():
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError("MISSING_MUSL_BINARY_SEED " + name)
    return p


def command(binaries, tool, *args):
    return ["qemu-riscv64", binaries[tool], *args]


def native_pp(binaries, src, target, macros, hdr, logs, env):
    argv = command(binaries, "minic-cpp", "-E", "-P", "-undef", "-nostdinc",
                   "-DNDEBUG", "-D__riscv_float_abi_double=1",
                   "-D__riscv_flen=64",
                   *["-I" + str(ROOT / x) for x in INCLUDES],
                   *["-I" + str(p) for p in hdr],
                   "-include", macros, "-o", target, ROOT / src)
    return execute(argv, logs, timeout=180, env=env)


def native_object(binaries, inp, asm, obj, log, env, *, as_timeout=240):
    compile_seconds = execute(command(binaries, "minic-cc", "-S", inp, "-o", asm),
                              log.with_suffix(".cc.log"), timeout=1100, env=env)
    as_seconds = execute(command(binaries, "minic-as", "-o", obj, asm),
                         log.with_suffix(".as.log"), timeout=as_timeout, env=env)
    if not asm.is_file() or not obj.is_file() or obj.stat().st_size < 64:
        raise RuntimeError("missing assembly/object " + str(inp))
    return compile_seconds, as_seconds


def source_key(source):
    return source.replace("/", "__").replace(".", "_")


def native_link_probe(binaries, source_lists, all_sources, seed_libraries, link_libraries, previous, out):
    """Isolate a deterministic native linker block from 77 certified objects.

    Diagnostic only. Never upgrades Stage3/4 self-host status on link probe.
    Externally provided libc/CRT inputs are still binary bootstrap seeds.
    """
    previous = previous.resolve()
    evidence = json.loads((previous / "stage3/summary.json").read_text())
    if (evidence.get("round") != "stage3" or
        evidence.get("passed") != len(all_sources) or
        evidence.get("failed") or
        evidence.get("source_count") != len(all_sources) or
        evidence.get("seed_binaries") != {k: sha(v) for k,v in binaries.items()} or
        evidence.get("runtime_seed") != {k: sha(v) for k,v in seed_libraries.items()}):
        raise RuntimeError("FULL_NATIVE_LINK_PROBE=FAIL prior object cohort or seed differs")
    rows = {r["source"]: r for r in evidence["rows"]}
    if set(rows) != set(all_sources):
        raise RuntimeError("FULL_NATIVE_LINK_PROBE=FAIL source inventory changed")
    src_hashes = {}
    for source in all_sources:
        key = source_key(source)
        asm = previous / "stage3/s" / (key + ".s")
        obj = previous / "stage3/o" / (key + ".o")
        if not asm.is_file() or not obj.is_file() or sha(asm) != rows[source]["s"] or sha(obj) != rows[source]["o"]:
            raise RuntimeError("FULL_NATIVE_LINK_PROBE=FAIL object SHA " + source)
        src_hashes[source] = sha(obj)
    print("FULL_NATIVE_LINK_PROBE_EVIDENCE=PASS source_objects=%d" % len(rows),flush=True)
    work = out / "link-probe"
    work.mkdir(parents=True,exist_ok=True)
    env = dict(os.environ, **CFLAGS)
    tool = "minic-cpp"   # low-TU, representative hosted target linking first
    objs = [previous / "stage3/o" / (source_key(s) + ".o")
            for s in source_lists[tool]]
    argv_prefix = ["-melf64lriscv", "-static", "-e", "_start"]
    candidates = {
        "original": [link_libraries["crt1.o"], link_libraries["crti.o"], *objs,
                     "--start-group", link_libraries["libc.a"], link_libraries["libgcc.a"],
                     "--end-group", link_libraries["crtn.o"]],
        "no_init_fini": [link_libraries["crt1.o"], *objs,
                         "--start-group", link_libraries["libc.a"], link_libraries["libgcc.a"],
                         "--end-group"],
        "crt_and_plain_libc": [link_libraries["crt1.o"], *objs,
                                link_libraries["libc.a"], link_libraries["libgcc.a"]],
    }
    report = {"seed_sha": {k:sha(v) for k,v in binaries.items()},
              "original_runtime_sha":{k:sha(v) for k,v in seed_libraries.items()},
              "link_runtime_sha":{k:sha(v) for k,v in link_libraries.items()},
              "object_count":len(rows), "object_sha":src_hashes,"variants":[]}
    passed = False
    for variant, link_inputs in candidates.items():
        elf = work / (tool + "-" + variant)
        argv = command(binaries, "minic-ld", *argv_prefix, "-o", elf, *link_inputs)
        log = work / (variant + ".log")
        begin = time.monotonic()
        try:
            p = subprocess.run(list(map(str,argv)),cwd=ROOT,env=env,
                               stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                               timeout=180,check=False)
            rc = p.returncode
            stderr = p.stderr.decode(errors="replace")
        except subprocess.TimeoutExpired:
            rc = 124
            stderr = "TIMEOUT"
        log.write_text(stderr)
        row = {"variant":variant, "rc":rc, "seconds":round(time.monotonic()-begin,3),
               "stderr_tail":stderr[-2000:],
               "elf_sha":sha(elf) if elf.is_file() else None}
        report["variants"].append(row)
        print("FULL_NATIVE_LINK_PROBE variant=%s rc=%d seconds=%.2f err=%s" %
              (variant,rc,row["seconds"],stderr[-500:].replace("\n"," | ")),flush=True)
        if rc==0 and elf.is_file():
            elf.chmod(0o755)
            execute(command({"probe":elf},"probe","--help"),work/(variant+".help.log"),
                    timeout=30,env=env,expected=0)
            passed=True
            break
    (work/"summary.json").write_text(json.dumps(report,indent=2)+"\n")
    if not passed:
        raise RuntimeError("FULL_NATIVE_LINK_PROBE=BLOCKED all variants failed; see stderr in artifact")
    print("FULL_NATIVE_LINK_PROBE=PASS tool=minic-cpp no_GNU_fallback=true",flush=True)


def native_stage4_runtime_smoke(binaries, root, env):
    """Functional PP/CC/AS/LD and driver proof, in addition to a fixed point.

    A --help result and identical loadable bytes are insufficient to prove that
    the freshly rebuilt tools genuinely compile, link and execute programs.
    This gate runs only self-built executables; no GCC/AS/LD subprocess.
    """
    work = root / "stage4" / "runtime-smoke"
    (work / "logs").mkdir(parents=True, exist_ok=True)
    header = work / "answer.h"
    c = work / "answer.c"
    pre = work / "answer.i"
    asm = work / "answer.s"
    obj = work / "answer.o"
    start_asm = work / "start.s"
    start_obj = work / "start.o"
    elf = work / "native42.elf"
    header.write_text("#define N 21\n#define DOUBLE(x) ((x)+(x))\n")
    c.write_text("#include \"answer.h\"\n#if DOUBLE(N) != 42\n#error native-preprocessor-wrong\n#endif\nint main(void) { return DOUBLE(N); }\n")
    start_asm.write_text(".text\n.globl _start\n.type _start, @function\n_start:\n  call main\n  li a7, 93\n  ecall\n.size _start, .-_start\n")
    steps = [
      ("pp", command(binaries, "minic-cpp", "-E", "-P", "-undef", "-nostdinc", "-o", pre, c), 90),
      ("cc", command(binaries, "minic-cc", "-S", pre, "-o", asm), 180),
      ("as", command(binaries, "minic-as", "-o", obj, asm), 120),
      ("start-as", command(binaries, "minic-as", "-o", start_obj, start_asm), 60),
      ("ld", command(binaries, "minic-ld", "-melf64lriscv", "-static", "-e", "_start", "-o", elf, start_obj, obj), 120),
      ("runtime", ["qemu-riscv64", elf], 45)
    ]
    for name, argv, timeout in steps:
        execute(argv, work / "logs" / (name + ".log"), timeout=timeout, env=env,
                expected=42 if name == "runtime" else 0)
    print("FULL_NATIVE_STAGE4_RUNTIME=PASS fresh_C_PP_CC_AS_LD_QEMU_exit42=true GNU_PP_AS_LD=false",flush=True)

    # Driver smoke: preserve exact new driver bytes but place host QEMU shims
    # beside it so guest subprocesses correctly invoke the new RV64 tools.
    driver_bin = work / "driver-bin"
    driver_bin.mkdir(exist_ok=True)
    guest_driver = driver_bin / "minic"
    shutil.copy2(binaries["minic"], guest_driver)
    guest_driver.chmod(0o755)
    if sha(guest_driver) != sha(binaries["minic"]):
        raise RuntimeError("stage4 driver copy SHA differs")
    for name in ("minic-cpp", "minic-cc", "minic-as", "minic-ld"):
        wrapper = driver_bin / name
        wrapper.write_text("#!/bin/sh\nexec qemu-riscv64 " +
                           shlex.quote(str(binaries[name])) + " \"$@\"\n")
        wrapper.chmod(0o755)
    driver_obj = work / "driver.o"
    driver_elf = work / "driver42.elf"
    execute(["qemu-riscv64", guest_driver, "-c", "-ffreestanding", "-nostdinc",
             "-o", driver_obj, c],
            work / "logs" / "driver-cc.log",timeout=300,env=env)
    execute(command(binaries,"minic-ld", "-melf64lriscv", "-static", "-e", "_start",
                    "-o", driver_elf, start_obj, driver_obj),
            work / "logs" / "driver-ld.log",timeout=120,env=env)
    execute(["qemu-riscv64", driver_elf],
            work / "logs" / "driver-runtime.log",timeout=45,env=env,expected=42)
    print("FULL_NATIVE_STAGE4_DRIVER=PASS fresh_C_PP_CC_AS_LD_QEMU_exit42=true GNU_fallback=false",flush=True)


def rounds(binaries, sources, all_sources, hdr, libraries, out, workers, probe=False, resume=None, link_libraries=None):
    if link_libraries is None:
        link_libraries = libraries
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
                           "\n#define __STRICT_ANSI__ 1\n"
                           "#undef __CHAR_UNSIGNED__\n"
                           "#undef __WCHAR_TYPE__\n"
                           "#define __WCHAR_TYPE__ int\n"
                           "#undef __SIZEOF_WCHAR_T__\n"
                           "#define __SIZEOF_WCHAR_T__ 4\n"
                           "#undef __WCHAR_MAX__\n"
                           "#define __WCHAR_MAX__ 2147483647\n"
                           "#undef __WCHAR_MIN__\n"
                           "#define __WCHAR_MIN__ (-2147483647-1)\n"
                           "#undef __riscv_float_abi_soft\n"
                           "#define __riscv_float_abi_double 1\n"
                           "#define __riscv_flen 64\n")
        completed = []
        failures = []
        prior_rows = {}
        prior_root = None
        if stage == 3 and resume is not None:
            prior_root = resume.resolve()
            previous = json.loads((prior_root / "stage3/summary.json").read_text())
            if (previous["round"] != "stage3" or
                previous["source_count"] != len(all_sources) or
                previous["seed_binaries"] != {k: sha(v) for k, v in prev.items()} or
                previous["runtime_seed"] != {k: sha(v) for k, v in libraries.items()}):
                raise RuntimeError("REUSE_STAGE3=FAIL previous evidence/seed mismatch")
            prior_rows = {row["source"]: row for row in previous["rows"]}
            if len(prior_rows) != len(previous["rows"]):
                raise RuntimeError("REUSE_STAGE3=FAIL repeated cached source")
            print("REUSE_STAGE3_EVIDENCE=PASS certified=%d previous_failures=%d" %
                  (len(prior_rows), len(previous["failed"])), flush=True)
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
            old = prior_rows.get(src)
            if old is not None and sha(inp) == old["i"]:
                old_asm = prior_root / "stage3/s" / (key + ".s")
                old_obj = prior_root / "stage3/o" / (key + ".o")
                if (old_asm.is_file() and old_obj.is_file() and
                    sha(old_asm) == old["s"] and sha(old_obj) == old["o"]):
                    shutil.copy2(old_asm, asm)
                    shutil.copy2(old_obj, obj)
                    return {"source": src, "pp_and_all_sec": round(time.monotonic()-begin, 3),
                            "cc_sec": 0.0, "as_sec": 0.0, "cached": True,
                            "i": sha(inp), "s": sha(asm), "o": sha(obj)}
            cc, assembler = native_object(prev, inp, asm, obj,
                                          work / "logs" / key, env,
                                          as_timeout=1200 if src == "src/core/core_lower.c" else 240)
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
                   "runtime_seed": {k: sha(v) for k,v in libraries.items()},
                   "link_runtime_seed": {k: sha(v) for k,v in link_libraries.items()},
                   "reused": sum(bool(r.get("cached")) for r in completed)}
        (work / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        if failures:
            raise RuntimeError("%s_PREPROCESS_COMPILE_ASSEMBLE blockers=%d first=%s" %
                               (name, len(failures), failures[0]))
        if probe:
            print("FULL_NATIVE_PROBE=PASS source_tus=%d stage3_mini_pp_cc_as=true" % len(completed), flush=True)
            return
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
                                link_libraries["crt1.o"], link_libraries["crti.o"], *objs,
                                "--start-group", link_libraries["libc.a"],
                                link_libraries["libgcc.a"], "--end-group",
                                link_libraries["crtn.o"])
            execute(link_args, work / "logs" / (tool + ".ld.log"),
                    timeout=480, env=env)
            if not elf.is_file() or elf.stat().st_size < 1024:
                raise RuntimeError("missing linked native tool " + tool)
            elf.chmod(0o755)
            # A native binary must genuinely start under QEMU.
            execute(command({"run": elf}, "run", "--help"),
                    work / "logs" / (tool + ".help.log"), timeout=60, env=env,
                    expected=0)
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
            native_stage4_runtime_smoke(next_binaries, out, env)
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
    parser.add_argument("--resume-stage3", type=Path, default=None,
                        help="Prior CI evidence directory containing stage3/summary.json, s/ and o/")
    parser.add_argument("--link-only", action="store_true", help="Link certified Stage3 77-object cohort only")
    parser.add_argument("--musl-build", type=Path, default=None, help="Previously built pinned RV64 static musl seed")
    parser.add_argument("--probe-only", default=None,
                        help="Comma-separated real production C units for native PP/CC/AS triage")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    sources, all_sources = inventory()
    stage2 = args.stage2.resolve()
    artifact = args.toolchain.resolve()
    seed = verify_seed(stage2, artifact)
    hdr, libraries = prebuilt_runtime()
    print("FULL_NATIVE_SEED=PASS tools=9 source_tus=%d stage2_sha=%s"
          " external_runtime=prebuilt_cross_libc_crt_libgcc_gcc_builtin_headers" %
          (len(all_sources), sha(stage2)), flush=True)
    if args.link_only:
        if args.resume_stage3 is None:
            raise RuntimeError("link-only requires --resume-stage3")
        link_libs = musl_runtime(args.musl_build, libraries) if args.musl_build else libraries
        native_link_probe(seed, sources, all_sources, libraries, link_libs, args.resume_stage3, out)
    elif args.probe_only is not None:
        selected = args.probe_only.split(",")
        if not selected or len(set(selected)) != len(selected) or any(x not in all_sources for x in selected):
            raise RuntimeError("invalid real production probe source selection")
        rounds(seed, sources, selected, hdr, libraries, out, args.workers, probe=True)
    else:
        link_libs = musl_runtime(args.musl_build, libraries) if args.musl_build else libraries
        rounds(seed, sources, all_sources, hdr, libraries, out, args.workers,
               resume=args.resume_stage3, link_libraries=link_libs)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, KeyError, ValueError) as exc:
        print("FULL_NATIVE_TOOLCHAIN=BLOCKED " + str(exc), file=sys.stderr, flush=True)
        sys.exit(1)
