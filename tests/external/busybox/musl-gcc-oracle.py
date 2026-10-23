#!/usr/bin/env python3
"""Reference GNU C compiler with exact musl (not glibc) headers and CRT.

Kbuild itself is original upstream. MiniC only replaces the actual real
C-owner objects downstream via the existing allyes_sweep/mixed_bulk oracle.
"""
import os
from pathlib import Path
import subprocess
import sys

def main(args):
    gcc = os.environ["MINIC_MUSL_GCC"]
    incl = Path(os.environ["MINIC_MUSL_SYSROOT"]) / "usr" / "include"
    builtin = os.environ["MINIC_MUSL_BUILTIN"]
    build = Path(os.environ["MINIC_MUSL_BUILD"])
    uapi = os.environ["MINIC_MUSL_UAPI"]
    libgcc = os.environ["MINIC_MUSL_LIBGCC"]
    if any(s in args for s in ("-dumpversion", "-dumpfullversion",
                               "-dumpmachine", "--version", "-v")):
        return subprocess.call([gcc, *args])
    headers = ["-nostdinc", "-isystem", str(incl), "-isystem", builtin,
               "-I" + uapi]
    if "-r" in args:
        return subprocess.call([gcc, "-nostdlib", *args])
    if any(s in args for s in ("-c", "-E", "-S", "-M", "-MM", "-MD",
                               "-MMD", "-fsyntax-only")):
        return subprocess.call([gcc, *headers, "-fno-pic", "-fno-pie", *args])
    # The exact same GNU musl link mode applies to the original Kbuild
    # trylink feature probes and the final BusyBox link.
    flags = [s for s in args if s not in (
        "-lm", "-lcrypt", "-lrt", "-lresolv", "-lpthread", "-ldl", "-lc")]
    crt = build / "obj" / "crt"
    return subprocess.call([
        gcc, *headers, "-nostdlib", "-static", "-no-pie", "-Wl,-e,_start",
        str(crt / "crt1.o"), str(crt / "crti.o"), *flags,
        "-Wl,--start-group", str(build / "lib" / "libc.a"),
        libgcc, "-Wl,--end-group", str(crt / "crtn.o")
    ])

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
