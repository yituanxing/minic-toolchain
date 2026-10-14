#!/usr/bin/env python3
"""Real Linux full-object MiniPP source substitution with GNU GCC frontend.

Discover every built RISC-V GNU-GCC C object from its saved Kbuild .o.cmd.
No predetermined 3352 count: the compiled Image graph, not a frozen plan,
is the authority. Every selected object must consume MiniPP's exact .i.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time


def digest(path: Path) -> dict:
    stat = path.stat()
    return {
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "size": stat.st_size,
        "mtime_ns": stat.st_mtime_ns,
    }


def predef_args(argv: list[str]) -> tuple[str, ...]:
    allowed = (
        "-march=", "-mabi=", "-std=", "-fshort-wchar",
        "-funsigned-char", "-fsigned-char", "-fno-PIE", "-fPIE",
        "-fno-pie", "-fpie", "-fPIC", "-fpic",
        "-fno-PIC", "-fno-pic", "-mcmodel=",
    )
    return tuple(x for x in argv if x.startswith(allowed))


def extract(out: Path, src: Path) -> list[dict]:
    entries = []
    seen = set()
    for file in sorted(out.rglob(".*.o.cmd")):
        relative = file.relative_to(out)
        basename = file.name
        if not basename.startswith(".") or not basename.endswith(".o.cmd"):
            continue
        obj = (relative.parent / basename[1:-4]).as_posix()
        data = file.read_text(errors="replace")
        pattern = r"(?m)^(?:savedcmd|cmd)_" + re.escape(obj) + r"\s*:=\s*(.*?)(?=\n(?:source_|deps_|cmd_|savedcmd_|\n)|\Z)"
        match = re.search(pattern, data, re.S)
        if match is None:
            # Non-C Kbuild-generated object whose command isn't a GCC compile.
            continue
        command = match.group(1).replace("\\\n", " ").strip()
        try:
            argv = shlex.split(command)
        except ValueError:
            continue
        if not argv or "riscv64-linux-gnu-gcc" not in argv[0] or "-c" not in argv:
            continue
        # GCC can use a *C file itself* as a forced -include, e.g.
        # arch/riscv/kernel/vdso/vgettimeofday.o includes the shared
        # lib/vdso/gettimeofday.c. The final .c positional argument
        # remains the *only* input translation unit. Do not count forced
        # inclusions as independently compiled source files.
        value_options = {
            "-include", "-imacros", "-I", "-isystem", "-iquote",
            "-idirafter", "-iprefix", "-iwithprefix", "-iwithprefixbefore",
            "-x", "-o", "-MF", "-MT", "-MQ", "-isysroot",
        }
        sources = []
        consumed = False
        for arg in argv[1:]:
            if consumed:
                consumed = False
                continue
            if arg in value_options:
                consumed = True
                continue
            if arg.endswith(".c") and not arg.startswith("-"):
                sources.append(arg)
        if not sources:
            # GCC is also the driver for RISC-V preprocessed assembler
            # inputs (.S) in Kbuild. These are not C translation units,
            # even though their saved command contains GCC and "-c".
            asm_source = any(a.endswith((".S", ".s", ".asm"))
                             for a in argv[1:] if not a.startswith("-"))
            assembler_mode = any(
                a in ("assembler", "assembler-with-cpp")
                for a in argv[1:]
            )
            if asm_source or assembler_mode:
                continue
            raise RuntimeError(f"GCC -c unknown non-C producer: {obj} argv={argv!r}")
        if len(sources) != 1:
            raise RuntimeError(f"ambiguous C inputs: {obj} {sources!r}")
        if "-o" not in argv:
            raise RuntimeError(f"missing -o: {obj}")
        source = sources[0]
        source_path = (out / source).resolve()
        if not source_path.is_file():
            raise RuntimeError(f"source not accessible: {obj} {source}")
        if (out / obj).is_file() is False:
            raise RuntimeError(f"built C object missing: {obj}")
        # The wrapper strips the real Linux source-tree prefix, not the
        # saved command prefix. Out-of-tree generated .c stay absolute.
        abs_src = str(src.resolve()) + os.sep
        selector = (
            str(source_path)[len(abs_src):]
            if str(source_path).startswith(abs_src)
            else (source[2:] if source.startswith("./") else source)
        )
        if obj in seen:
            raise RuntimeError(f"duplicate object: {obj}")
        seen.add(obj)
        entries.append(dict(object=obj, source=source, selector=selector,
                            args=argv[1:], context=predef_args(argv[1:])))
    if len(entries) < 1000:
        raise RuntimeError(f"too few real Kbuild GNU C objects for full replacement: {len(entries)}")
    return entries


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--src", type=Path, required=True)
    p.add_argument("--stage", type=Path, required=True)
    p.add_argument("--wrapper", type=Path, required=True)
    p.add_argument("--predefines-script", type=Path, required=True)
    p.add_argument("--jobs", type=int, default=6)
    args = p.parse_args()
    out = args.out.resolve()
    src = args.src.resolve()
    stage = args.stage.resolve()
    stage.mkdir(parents=True, exist_ok=True)
    (stage / "stderr").mkdir(exist_ok=True)
    entries = extract(out, src)
    selector_file = stage / "selected-command-sources.txt"
    selector_file.write_text("\n".join(sorted({x["selector"] for x in entries})) + "\n")
    (stage / "inventory.tsv").write_text("object\tsource\tselector\n" + "".join(
        f'{x["object"]}\t{x["source"]}\t{x["selector"]}\n' for x in entries
    ))
    contexts = {}
    for row in entries:
        values = row["context"]
        key = hashlib.sha256(json.dumps(values).encode()).hexdigest()[:20]
        row["context_id"] = key
        contexts[key] = values
    print(f"MINIPP_NATIVE_FULL_INVENTORY=PASS objects={len(entries)} "
          f"unique_sources={len({x['selector'] for x in entries})} "
          f"predefine_contexts={len(contexts)}", flush=True)
    (stage / "predefines").mkdir(exist_ok=True)
    for key, flags in sorted(contexts.items()):
        predef = stage / "predefines" / f"{key}.h"
        env = os.environ.copy()
        env.update(REAL_CC="riscv64-linux-gnu-gcc",
                   MINIPP_LINUX_SRC=str(src), MINIPP_PREDEFINES_OUT=str(predef))
        subprocess.run(["bash", str(args.predefines_script.resolve()), *flags],
                       env=env, check=True, stdout=subprocess.DEVNULL)
        if predef.stat().st_size == 0:
            raise RuntimeError(f"empty GCC context: {key}")
    print(f"MINIPP_NATIVE_FULL_PREDEFINES=PASS contexts={len(contexts)}", flush=True)
    trace = stage / "minipp.trace"
    trace.write_text("")
    common = os.environ.copy()
    common.update(
        MINIPP_LINUX_SELECT_FILE=str(selector_file),
        MINIPP_LINUX_SELECT_MODE="strict",
        MINIPP_LINUX_SUBSTITUTE="true",
        MINIPP_LINUX_SRC=str(src),
        MINIPP_LINUX_WORK=str(stage / "inputs"),
        MINIPP_LINUX_TRACE=str(trace),
        MINIPP=str(Path(os.environ["MINI_TOOLS_BIN"]) / "minic-cpp"),
        MINIAS_GNU_ROOT=os.environ["MINIAS_GNU_ROOT"],
        REAL_CC="riscv64-linux-gnu-gcc",
        REFERENCE_CPP="riscv64-linux-gnu-gcc",
    )
    started = time.monotonic()

    def run_one(row: dict) -> dict:
        # Unique stable filenames even when the same .c is compiled with
        # different output/flag contexts.
        key = hashlib.sha256(row["object"].encode()).hexdigest()[:20]
        env = common.copy()
        env["MINIPP_LINUX_INPUT_KEY"] = key
        env["MINIPP_LINUX_PREDEFINES_FILE"] = str(
            stage / "predefines" / f'{row["context_id"]}.h'
        )
        proc = subprocess.run(
            ["bash", str(args.wrapper.resolve()), *row["args"]],
            cwd=out, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, errors="replace",
        )
        result = dict(object=row["object"], source=row["source"],
                      rc=proc.returncode, seconds=round(time.monotonic()-started, 3),
                      status="PASS" if proc.returncode == 0 else "FAIL")
        if proc.returncode:
            (stage / "stderr" / f"{key}.txt").write_text(
                f'object={row["object"]}\nsource={row["source"]}\n'
                f'command={shlex.join(row["args"])}\n'
                f"stdout:\n{proc.stdout[-6000:]}\nstderr:\n{proc.stderr[-9000:]}\n"
            )
        return result

    results = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(run_one, row): row for row in entries}
        for f in as_completed(futures):
            results.append(f.result())
    results.sort(key=lambda r: r["object"])
    (stage / "results.tsv").write_text("object\tsource\tstatus\trc\tseconds\n" + "".join(
        f'{x["object"]}\t{x["source"]}\t{x["status"]}\t{x["rc"]}\t{x["seconds"]}\n'
        for x in results
    ))
    failures = [x for x in results if x["status"] != "PASS"]
    (stage / "failure-pool.tsv").write_text("object\tsource\tstatus\n" + "".join(
        f'{x["object"]}\t{x["source"]}\t{x["status"]}\n' for x in failures
    ))
    lines = trace.read_text(errors="replace").splitlines()
    exact = sum("status=EXACT" in x for x in lines if x.startswith("RESULT "))
    substituted = sum(x.startswith("SUBSTITUTE ") for x in lines)
    mismatch = sum("status=DIFF" in x or "status=MINIPP_FAIL" in x
                   for x in lines if x.startswith("RESULT "))
    summary = dict(expected=len(entries), succeeded=len(results)-len(failures),
                   failed=len(failures), exact=exact, substituted=substituted,
                   diff_or_pp_fail=mismatch, predefine_contexts=len(contexts),
                   wall_seconds=round(time.monotonic()-started, 2))
    (stage / "summary.json").write_text(json.dumps(summary, indent=2, sort_keys=True)+"\n")
    print("MINIPP_NATIVE_FULL_TUS "+json.dumps(summary, sort_keys=True), flush=True)
    for row in failures[:20]:
        print(f"MINIPP_NATIVE_FULL_FAILURE object={row['object']} source={row['source']}", flush=True)
    if failures or exact != len(entries) or substituted != len(entries) or mismatch:
        print("MINIPP_NATIVE_FULL_OBJECTS=FAIL reason=non-exact-or-missing-production",
              file=sys.stderr, flush=True)
        return 1
    objects = {row["object"]: digest(out / row["object"]) for row in entries}
    (stage / "production-object-identity.json").write_text(
        json.dumps(objects, indent=2, sort_keys=True)+"\n"
    )
    print(f"MINIPP_NATIVE_FULL_OBJECTS=PASS objects={len(entries)} "
          f"exact={exact} substituted={substituted} fallback=0", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
