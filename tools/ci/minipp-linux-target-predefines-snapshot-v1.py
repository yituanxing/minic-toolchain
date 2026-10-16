#!/usr/bin/env python3
"""Capture exact, immutable GNU builtin macro profiles for real RV64 Linux owners.

Diagnostic producer ONLY. A later native producer consumes a pinned snapshot,
never calls GCC to construct its input. The original GNU baseline is immutable.
No source .o or cached MiniC artefact is rebuilt by this capture.
"""
from __future__ import annotations

import argparse
import base64
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/preprocessor"))
from run_linux_native_full_production import extract, predef_args  # noqa: E402

AS_CONTEXT_PREFIXES = (
    "-march=", "-mabi=", "-mcmodel=", "-std=",
    "-fno-PIE", "-fPIE", "-fno-pie", "-fpie",
    "-fPIC", "-fpic", "-fno-PIC", "-fno-pic",
    "-ffreestanding", "-fhosted", "-fno-builtin",
    "-fbuiltin", "-fshort-wchar", "-funsigned-char", "-fsigned-char",
)
DYNAMIC_CONTEXT_PREFIXES = AS_CONTEXT_PREFIXES
OPT = {"-O0", "-O1", "-O2", "-O3", "-Og", "-Os", "-Oz", "-Ofast"}
DYNAMIC = {
    "arch/riscv/kernel/alternative.o",
    "arch/riscv/kernel/signal.o",
    "arch/riscv/kernel/compat_signal.o",
    "arch/riscv/kernel/vdso.o",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def as_owners(golden: Path, c_owners: set[str]):
    for cmd in sorted(golden.rglob(".*.cmd")):
        name = (cmd.parent / cmd.name[1:-4]).relative_to(golden).as_posix()
        if name in c_owners or not name.endswith(".o"):
            continue
        data = cmd.read_text(errors="replace")
        if not re.search(r"(?m)^source_[^\\n]*\\s*:=\\s*\\S+\\.(?:S|s)\\s*$", data):
            continue
        lines = [x.split(":=", 1)[1].strip() for x in data.splitlines()
                 if re.match(r"^(?:savedcmd|cmd)_[^\\n]+\\s*:=", x)]
        if len(lines) != 1:
            continue
        try:
            argv = shlex.split(lines[0])
        except ValueError:
            continue
        if not argv or Path(argv[0]).name != "riscv64-linux-gnu-gcc":
            continue
        yield name, argv


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--golden", type=Path, required=True)
    p.add_argument("--linux-src", type=Path, required=True)
    p.add_argument("--owners", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    golden = a.golden.resolve()
    src = a.linux_src.resolve()
    c_owners = {line.strip() for line in a.owners.read_text().splitlines()
                if line.strip()}
    if len(c_owners) != 2064 or not golden.is_dir() or not src.is_dir():
        raise SystemExit("MINIPP_PROFILE_CAPTURE=FAIL invalid certified Linux fixtures")
    entries = {x["object"]: x for x in extract(golden, src)}
    if not c_owners <= entries.keys():
        raise SystemExit("MINIPP_PROFILE_CAPTURE=FAIL incomplete 2064 Kbuild C universe")

    requests: dict[tuple[str, tuple[str, ...]], set[str]] = {}
    for name in sorted(c_owners):
        args = entries[name]["args"]
        key = ("c", tuple(predef_args(args)))
        requests.setdefault(key, set()).add(name)
        if name in DYNAMIC:
            dyn = tuple(x for x in args if x.startswith(DYNAMIC_CONTEXT_PREFIXES)
                        or x in OPT)
            requests.setdefault(("c", dyn), set()).add("dynamic:"+name)
    s_count = 0
    for name, argv in as_owners(golden, c_owners):
        context = tuple(x for x in argv if x.startswith(AS_CONTEXT_PREFIXES)
                        or x in OPT)
        requests.setdefault(("assembler-with-cpp", context), set()).add(name)
        s_count += 1
    if not 30 <= s_count <= 500:
        raise SystemExit("MINIPP_PROFILE_CAPTURE=FAIL implausible actual .S universe "
                         + str(s_count))
    output = {"schema": "minipp-rv64-linux-gcc13-profile-v1",
              "linux": "6.6.143", "c_owners": len(c_owners),
              "s_owners": s_count, "profiles": []}
    gcc = subprocess.run(["riscv64-linux-gnu-gcc", "-dumpfullversion", "-dumpversion"],
                         capture_output=True, text=True, check=True)
    output["gcc_version"] = gcc.stdout.strip()
    with tempfile.TemporaryDirectory(prefix="minipp-profile-") as folder:
        dest = Path(folder)
        for idx, ((language, context), owners) in enumerate(sorted(requests.items())):
            path = dest / f"profile-{idx:03}.h"
            env = os.environ.copy()
            env.update(REAL_CC="riscv64-linux-gnu-gcc",
                       MINIPP_LINUX_SRC=str(src),
                       MINIPP_PREDEFINES_OUT=str(path),
                       MINIPP_PREDEFINES_LANG=language)
            proc = subprocess.run(
                ["bash", str(ROOT / "tests/preprocessor/build_reference_predefines.sh"),
                 *context], cwd=ROOT, env=env, capture_output=True, text=True)
            if proc.returncode or not path.is_file():
                raise SystemExit("MINIPP_PROFILE_CAPTURE=FAIL context="+repr(context)
                                 +" language="+language+" stderr="+proc.stderr[-2200:])
            body = path.read_text()
            output["profiles"].append({
                "language": language, "flags": list(context),
                "content": body, "sha256": digest(body.encode()),
                "owners": sorted(owners), "owner_count": len(owners),
            })
            print("MINIPP_PROFILE_CONTEXT=PASS language="+language+
                  " flags="+repr(context)+" owners="+str(len(owners))+
                  " sha="+digest(body.encode())[:16], flush=True)

    raw = (json.dumps(output, sort_keys=True, separators=(",", ":"))+"\\n").encode()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(raw)
    packed = base64.b64encode(gzip.compress(raw, mtime=0)).decode("ascii")
    print(f"MINIPP_PROFILE_CAPTURE=PASS profiles={len(output['profiles'])} "
          f"C={len(c_owners)} S={s_count} raw={len(raw)} gzip_b64={len(packed)} "
          f"manifest_sha256={digest(raw)} gcc_version={output['gcc_version']}",
          flush=True)
    print("MINIPP_PROFILE_CAPTURE_B64_BEGIN", flush=True)
    for pos in range(0, len(packed), 3200):
        print("MINIPP_PROFILE_CAPTURE_B64 "+packed[pos:pos+3200], flush=True)
    print("MINIPP_PROFILE_CAPTURE_B64_END", flush=True)


if __name__ == "__main__":
    main()
