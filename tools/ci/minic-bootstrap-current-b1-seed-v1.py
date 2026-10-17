#!/usr/bin/env python3
"""Recover historic B1 Stage0 -> Stage1 compiler self-host SEED on current sources.

This is NOT the Stage1 -> Stage2 fixed-point test. The old B1 workflow in
workflows-disabled contains that further step. GNU -E/AS/link and qemu-user
are intentionally retained as bootstrap oracles; this does not replace all GNU
toolchain components. This script does not patch production C sources.

Run: python3 tools/ci/minic-bootstrap-current-b1-seed-v1.py --work build/bootstrap-current-b1
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def command(args, *, output=None, env=None, timeout=240, stdout=None, stderr=None):
    start = time.monotonic()
    try:
        p = subprocess.run(
            list(map(str, args)), cwd=ROOT, env=env, stdout=stdout,
            stderr=stderr, timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        raise RuntimeError("timeout command=" + " ".join(map(str, args[:4]))) from None
    if p.returncode != 0:
        raise RuntimeError(
            "command rc=" + str(p.returncode) +
            " args=" + " ".join(map(str, args[:5])) +
            (" stderr=" + str(stderr.name) if stderr else ""))
    return time.monotonic() - start


def sources():
    mfile = (ROOT / "Makefile").read_text()
    m = re.search(
        r"(?ms)^MINIC_SOURCES\s*:=\s*\\\s*\n(.*?)(?=^MINIC_OBJECTS\s*:)",
        mfile)
    if m is None:
        raise RuntimeError("MINIC_SOURCES missing from Makefile")
    wanted = [x.strip().rstrip("\\").strip()
              for x in m.group(1).splitlines() if x.strip()]
    actual = [x.strip() for x in
              (ROOT / "tests/compiler/bootstrap/b0-sources.txt").read_text().splitlines()
              if x.strip() and not x.lstrip().startswith("#")]
    if wanted != actual or len(actual) != len(set(actual)):
        raise RuntimeError(
            "source manifest != Makefile compiler sources; make_count=" +
            str(len(wanted)) + " manifest_count=" + str(len(actual)) +
            " missing=" + str(sorted(set(wanted)-set(actual))) +
            " extra=" + str(sorted(set(actual)-set(wanted))))
    if (not actual or actual[-1] != "tools/minic-cc/main.c" or
            "src/core/core_lower_bool.c" not in actual or
            "tools/minic/main.c" in actual):
        raise RuntimeError("compiler stage source list/entry is not canonical")
    for src in actual:
        if not (ROOT / src).is_file():
            raise RuntimeError("missing source " + src)
    print("BOOTSTRAP_B1_MANIFEST=PASS sources=" + str(len(actual)) +
          " exact_make_order=true compiler_entry=minic-cc", flush=True)
    return actual


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--work", type=Path, default=Path("build/bootstrap-current-b1"))
    args = p.parse_args()
    os.chdir(ROOT)
    for tool in ("make", "gcc", "riscv64-linux-gnu-gcc",
                 "qemu-riscv64", "sha256sum"):
        if shutil.which(tool) is None:
            raise RuntimeError("missing prerequisite " + tool)
    selected = sources()
    work = args.work.resolve()
    stage0 = work / "stage0" / "bin" / "minic-cc"
    seed = work / "seed"
    for x in (seed / "i", seed / "s", seed / "logs"):
        x.mkdir(parents=True, exist_ok=True)
    (seed / "sources.normalized").write_text("\n".join(selected) + "\n")
    logs = seed / "logs"
    env = dict(os.environ,
               MINIC_LINUX_FULL_CROSSBLOCK="1",
               MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ="1")
    buildlog = logs / "stage0-build.log"
    with buildlog.open("wb") as log:
        try:
            secs = command(
                ["make", "-j4", "MODE=release", "CFLAGS=-Werror",
                 "BUILD_DIR=" + str(work / "stage0"), str(stage0),
                 "check-type", "check-ast-contract"],
                env=env, timeout=720, stdout=log, stderr=subprocess.STDOUT)
        except RuntimeError:
            sys.stderr.write(buildlog.read_text(errors="replace")[-7000:])
            raise
    if not stage0.is_file():
        raise RuntimeError("Stage0 compiler binary missing")
    (seed / "stage0-binary.sha256").write_text(sha(stage0) + "  " + str(stage0) + "\n")
    print("BOOTSTRAP_B1_STAGE0=PASS build_seconds=" + str(round(secs, 3)) +
          " sha256=" + sha(stage0), flush=True)

    manifest_rows = []
    input_rows = []
    assemblies = []
    used_keys = set()
    for index, source in enumerate(selected):
        key = source.replace("/", "__").replace(".", "_")
        if key in used_keys:
            raise RuntimeError("colliding source key " + key)
        used_keys.add(key)
        pre = seed / "i" / (key + ".i")
        asm = seed / "s" / (key + ".s")
        err = logs / (key + ".stderr")
        with err.open("wb") as e:
            command(
                ["riscv64-linux-gnu-gcc", "-E", "-P", "-std=c11",
                 "-Iinclude", "-Isrc", source, "-o", pre],
                env=env, stdout=subprocess.DEVNULL, stderr=e, timeout=180)
        if not pre.is_file() or pre.stat().st_size == 0:
            raise RuntimeError("empty genuine preprocessed .i for " + source)
        with err.open("wb") as e:
            start = time.monotonic()
            try:
                command([stage0, "-S", pre, "-o", asm], env=env,
                        timeout=180, stdout=subprocess.DEVNULL, stderr=e)
            except RuntimeError as exc:
                sys.stderr.write("BOOTSTRAP_B1_STAGE0_TU=FAIL source=" + source +
                                 " details=" + str(exc) + "\n" +
                                 err.read_text(errors="replace")[-7000:])
                raise
            secs = time.monotonic() - start
        if not asm.is_file() or asm.stat().st_size == 0:
            raise RuntimeError("empty assembly for " + source)
        manifest_rows.append(key + "\t" + source + "\t" + sha(asm))
        input_rows.append(key + "\t" + source + "\t" + sha(pre))
        assemblies.append(asm)
        (logs / (key + ".time")).write_text("wall_s=" + str(round(secs, 3)) + "\n")
        print("BOOTSTRAP_B1_STAGE0_TU=PASS " + str(index + 1) + "/" +
              str(len(selected)) + " source=" + source +
              " wall_s=" + str(round(secs, 3)), flush=True)

    (seed / "stage0-manifest.tsv").write_text("\n".join(manifest_rows) + "\n")
    (seed / "input-sha256.tsv").write_text("\n".join(input_rows) + "\n")
    stage1 = seed / "minic-stage1-rv64"
    with (logs / "stage1-link.stderr").open("wb") as e:
        command(["riscv64-linux-gnu-gcc", "-static", *assemblies, "-o", stage1],
                env=env, timeout=360, stdout=subprocess.DEVNULL, stderr=e)
    if not stage1.is_file():
        raise RuntimeError("no Stage1 linked binary")
    (seed / "stage1-binary.sha256").write_text(sha(stage1) + "  " + str(stage1) + "\n")
    print("BOOTSTRAP_B1_STAGE1_LINK=PASS selected=" + str(len(selected)) +
          " sha256=" + sha(stage1), flush=True)

    pre = seed / "return_42.i"
    asm = seed / "return_42.stage1.s"
    elf = seed / "return_42.stage1.elf"
    command(["riscv64-linux-gnu-gcc", "-E", "-P", "-x", "c",
             "tests/compiler/c0/return_42.c", "-o", pre], env=env)
    with (logs / "stage1-smoke.stderr").open("wb") as e:
        command(["qemu-riscv64", stage1, "-S", pre, "-o", asm],
                env=env, timeout=240, stdout=subprocess.DEVNULL, stderr=e)
    command(["riscv64-linux-gnu-gcc", "-static", asm, "-o", elf],
            env=env, timeout=120)
    try:
        ret = subprocess.run(["qemu-riscv64", str(elf)], cwd=ROOT,
                             env=env, timeout=60, check=False).returncode
    except subprocess.TimeoutExpired:
        raise RuntimeError("return_42 runtime timed out") from None
    if ret != 42:
        raise RuntimeError("Stage1 return_42 smoke got " + str(ret))
    (seed / "return_42.stage1.sha256").write_text(sha(asm) + "  " + str(asm) + "\n")
    print("BOOTSTRAP_B1_STAGE1_EXEC=PASS return_42=42", flush=True)
    print("BOOTSTRAP_B1_SEED=PASS selected=" + str(len(selected)) +
          " stage0_asm=" + str(len(assemblies)) +
          " stage1_runtime=PASS stage2=NOT_RUN", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError) as exc:
        print("BOOTSTRAP_B1_SEED=FAIL " + str(exc), file=sys.stderr)
        sys.exit(1)
