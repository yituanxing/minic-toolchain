#!/usr/bin/env python3
"""Current MiniC Stage1 -> Stage2 B1 exact fixed point, adapted from frozen B1.

Reuses the Stage0-generated .i and .s SHA manifest and static Stage1 produced
by minic-bootstrap-current-b1-seed-v1.py. All 47 Stage1 recompilations run
under qemu-riscv64. Require 47/47 byte-identical assembly, Stage1/Stage2
identical loadable machine images, and Stage2 return_42=42.

NOT a proof of no GNU dependencies: cross gcc preprocessing, assembly,
linking, CRT, and libc remain official bootstrapping oracles.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def hashfile(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run(argv, *, env, timeout=1200, log=None):
    start = time.monotonic()
    try:
        if log is None:
            result = subprocess.run([str(x) for x in argv], cwd=ROOT,
                                    env=env, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.PIPE, timeout=timeout,
                                    check=False)
        else:
            with Path(log).open("wb") as f:
                result = subprocess.run([str(x) for x in argv], cwd=ROOT,
                                        env=env, stdout=f, stderr=subprocess.STDOUT,
                                        timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        raise RuntimeError("timeout command=" + " ".join(map(str, argv[:4]))) from None
    if result.returncode != 0:
        detail = Path(log).read_text(errors="replace")[-2400:] if log else result.stderr[-2400:].decode(errors="replace")
        raise RuntimeError("failed rc=" + str(result.returncode) +
                           " cmd=" + " ".join(map(str, argv[:4])) +
                           "\n" + detail)
    return round(time.monotonic() - start, 3)


def main():
    cli = argparse.ArgumentParser()
    cli.add_argument("--work", type=Path, default=Path("build/bootstrap-current-b1"))
    cli.add_argument("--workers", type=int, choices=(1,2,4), default=4)
    ns = cli.parse_args()
    os.chdir(ROOT)
    for binary in ("qemu-riscv64", "riscv64-linux-gnu-gcc",
                   "riscv64-linux-gnu-objcopy"):
        if not shutil.which(binary):
            raise RuntimeError("missing prerequisite " + binary)
    work = ns.work.resolve()
    seed = work / "seed"
    stage2dir = work / "stage2"
    s2 = stage2dir / "s"
    logdir = stage2dir / "logs"
    s2.mkdir(parents=True, exist_ok=True)
    logdir.mkdir(parents=True, exist_ok=True)
    stage1 = seed / "minic-stage1-rv64"
    if not stage1.is_file() or stage1.stat().st_size == 0:
        raise RuntimeError("missing certified Stage1 seed")
    selfhash = (seed / "stage1-binary.sha256").read_text().split()[0]
    if hashfile(stage1) != selfhash:
        raise RuntimeError("Stage1 ELF hash drift")
    rows = []
    for line in (seed / "stage0-manifest.tsv").read_text().splitlines():
        fields = line.split("\t")
        if len(fields) != 3:
            raise RuntimeError("bad canonical source manifest row")
        rows.append(tuple(fields))
    inputs = {}
    for line in (seed / "input-sha256.tsv").read_text().splitlines():
        fields = line.split("\t")
        if len(fields) != 3:
            raise RuntimeError("bad .i hash manifest row")
        inputs[(fields[0], fields[1])] = fields[2]
    expected_sources = (seed / "sources.normalized").read_text().splitlines()
    if len(rows) != len(expected_sources) or [r[1] for r in rows] != expected_sources \
       or len(set(r[0] for r in rows)) != len(rows):
        raise RuntimeError("source manifest diverges from seeded compiler C inputs")
    if expected_sources[-1] != "tools/minic-cc/main.c" \
       or "src/core/core_lower_bool.c" not in expected_sources:
        raise RuntimeError("compiler entry / LowerBool not certified")
    for key, source, before in rows:
        pre = seed / "i" / (key + ".i")
        asm = seed / "s" / (key + ".s")
        if ((key, source) not in inputs or not pre.is_file() or
                hashfile(pre) != inputs[(key, source)] or not asm.is_file() or
                hashfile(asm) != before):
            raise RuntimeError("Stage0 input/assembly SHA drift " + source)
    print("BOOTSTRAP_B1_REPLAY_INPUT=PASS selected=" + str(len(rows)) +
          " frozen_i_sha=PASS frozen_s_sha=PASS", flush=True)
    env = dict(os.environ, MINIC_LINUX_FULL_CROSSBLOCK="1",
               MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ="1")
    events = []

    def replay(row):
        key, source, expected = row
        path = s2 / (key + ".s")
        pre = seed / "i" / (key + ".i")
        sec = run(["qemu-riscv64", stage1, "-S", pre, "-o", path], env=env,
                  log=logdir / (key + ".log"), timeout=1200)
        if not path.is_file():
            raise RuntimeError("missing Stage2 assembly " + source)
        obtained = hashfile(path)
        if obtained != expected:
            raise RuntimeError("fixed-point SHA mismatch " + source +
                               " expected=" + expected + " got=" + obtained)
        return source, key, sec

    with ThreadPoolExecutor(max_workers=ns.workers) as pool:
        tasks = {pool.submit(replay, row): row for row in rows}
        for future in as_completed(tasks):
            try:
                src, key, secs = future.result()
            except Exception as exc:
                raise RuntimeError("BOOTSTRAP_B1_REPLAY_BLOCKER source=" +
                                   tasks[future][1] + " " + str(exc)) from None
            events.append((src,key,secs))
            print("BOOTSTRAP_B1_REPLAY_PASS count=" + str(len(events)) +
                  "/" + str(len(rows)) + " source=" + src +
                  " wall_s=" + str(secs), flush=True)
    if len(events) != len(rows):
        raise RuntimeError("incomplete replay set")
    (stage2dir / "replay-times.tsv").write_text(
        "source\tkey\twall_s\n" +
        "\n".join(src + "\t" + key + "\t" + str(t) for src,key,t in sorted(events)) + "\n")
    asm_paths = [s2 / (key + ".s") for key, source, expected in rows]
    stage2 = stage2dir / "minic-stage2-rv64"
    run(["riscv64-linux-gnu-gcc", "-static", *asm_paths, "-o", stage2],
        env=env, timeout=360, log=logdir / "stage2-link.log")
    print("BOOTSTRAP_B1_STAGE2_LINK=PASS selected=" + str(len(rows)) +
          " sha256=" + hashfile(stage2), flush=True)
    binary1 = stage2dir / "minic-stage1.load.bin"
    binary2 = stage2dir / "minic-stage2.load.bin"
    run(["riscv64-linux-gnu-objcopy", "-O", "binary", stage1, binary1], env=env)
    run(["riscv64-linux-gnu-objcopy", "-O", "binary", stage2, binary2], env=env)
    if hashfile(binary1) != hashfile(binary2):
        raise RuntimeError(
            "Stage1/Stage2 ELF loadable image mismatch " +
            hashfile(binary1) + " != " + hashfile(binary2))
    print("BOOTSTRAP_B1_FIXED_POINT=PASS selected=" + str(len(rows)) +
          " stage1_stage2_asm=exact loadable_image=exact" +
          " stage1_load_sha256=" + hashfile(binary1), flush=True)
    pre = seed / "return_42.i"
    if not pre.is_file():
        raise RuntimeError("missing pinned return_42.i")
    asm = stage2dir / "return_42.stage2.s"
    run(["qemu-riscv64", stage2, "-S", pre, "-o", asm],
        env=env, timeout=240, log=logdir / "stage2-smoke.log")
    if hashfile(asm) != hashfile(seed / "return_42.stage1.s"):
        raise RuntimeError("return_42 Stage1/Stage2 generated assembly differs")
    elf = stage2dir / "return_42.stage2.elf"
    run(["riscv64-linux-gnu-gcc", "-static", asm, "-o", elf], env=env)
    result = subprocess.run(["qemu-riscv64", str(elf)], cwd=ROOT, env=env,
                            timeout=60, check=False)
    if result.returncode != 42:
        raise RuntimeError("Stage2 return_42 status=" + str(result.returncode))
    (stage2dir / "stage2-binary.sha256").write_text(hashfile(stage2)+"  "+str(stage2)+"\n")
    (stage2dir / "stage2-fixed-point.txt").write_text(
        "stage1_elf_sha256=" + hashfile(stage1) + "\n" +
        "stage2_elf_sha256=" + hashfile(stage2) + "\n" +
        "loadable_image_sha256=" + hashfile(binary1) + "\n" +
        "source_count=" + str(len(rows)) + "\n")
    print("BOOTSTRAP_B1_STAGE2_EXEC=PASS return_42=42", flush=True)
    print("BOOTSTRAP_B1=PASS selected=" + str(len(rows)) +
          " selfhost_stage1_stage2_fixed_point=true" +
          " runtime_smoke=PASS full_linux_stage2=NOT_RUN", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError,OSError,subprocess.TimeoutExpired) as exc:
        print("BOOTSTRAP_B1=FAIL " + str(exc), file=sys.stderr)
        sys.exit(1)
