#!/usr/bin/env python3
"""Read-only BusyBox 1.36.1 allyesconfig MiniC frontier via real Kbuild CC invocations.

Invoke as a controller from the existing pinned BusyBox probe, or as the
CC proxy selected by make. The proxy ALWAYS calls the original GCC compiler
with the original arguments; MiniC only observes an independent -E -P
preprocessed snapshot and never supplies any object to the GNU build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


def compiler_argv():
    return shlex.split(os.environ.get("BUSYBOX_ALLYES_HOST_CC", "cc"))


def call_proxy(args):
    """Preserve GCC behavior; side-channel diagnostics cannot break GCC."""
    gcc_argv = compiler_argv()
    gcc = subprocess.run(gcc_argv + args, check=False)
    source = next((arg for arg in args if arg.endswith(".c")
                   and not arg.startswith("-")), None)
    if "-c" not in args or source is None:
        return gcc.returncode
    root = Path(os.environ["BUSYBOX_ALLYES_REPORT_ROOT"])
    key = hashlib.sha256((os.getcwd() + "\0" + " ".join(args)).encode()).hexdigest()[:16]
    target = root / "per-tu" / key
    target.mkdir(parents=True, exist_ok=True)
    obj = ""
    for idx, val in enumerate(args[:-1]):
        if val == "-o":
            obj = args[idx + 1]
    info = {"source": source, "object": obj, "gcc": gcc.returncode,
            "cpp": None, "minic": None, "assembler": None}
    # Keep -include/-isystem/-I/-D/-U, source and upstream language flags.
    # Remove the original object output and dependency-writing flags.
    cpp_flags = []
    skip = False
    for i, flag in enumerate(args):
        if skip:
            skip = False
            continue
        if flag in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if flag in ("-c", "-MD", "-MMD", "-MP", "-MG", "-MM", "-M"):
            continue
        if flag.startswith(("-MF", "-MT", "-MQ")):
            continue
        cpp_flags.append(flag)
    preprocessed = target / "input.i"
    asm = target / "output.s"
    obj_mini = target / "mini.o"
    with (target / "cpp.err").open("wb") as e:
        cpp = subprocess.run(gcc_argv + cpp_flags + ["-E", "-P", "-o", str(preprocessed)],
                             stderr=e, stdout=subprocess.DEVNULL, check=False)
    info["cpp"] = cpp.returncode
    if cpp.returncode == 0:
        with (target / "minic.err").open("wb") as e:
            mini = subprocess.run(
                [os.environ["BUSYBOX_ALLYES_MINIC_CC"],
                 "--target=x86_64-linux", "-S", str(preprocessed), "-o", str(asm)],
                stdout=subprocess.DEVNULL, stderr=e, check=False)
        info["minic"] = mini.returncode
        if mini.returncode == 0:
            with (target / "as.err").open("wb") as e:
                ass = subprocess.run(gcc_argv + ["-c", "-x", "assembler",
                                                str(asm), "-o", str(obj_mini)],
                                     stdout=subprocess.DEVNULL, stderr=e, check=False)
            info["assembler"] = ass.returncode
    (target / "result.json").write_text(json.dumps(info, sort_keys=True), encoding="utf-8")
    return gcc.returncode


def print_frontier(report_root, gcc_success, enabled):
    results = []
    for path in (report_root / "per-tu").glob("*/result.json"):
        results.append((path, json.loads(path.read_text(encoding="utf-8"))))
    results.sort(key=lambda x: (x[1]["source"], x[1]["object"]))
    stats = {"minic_object_pass": 0, "gcc_fail": 0, "cpp_fail": 0,
             "minic_fail": 0, "assembler_fail": 0}
    for path, info in results:
        if info["gcc"] != 0:
            stats["gcc_fail"] += 1
        if info["cpp"] != 0:
            stats["cpp_fail"] += 1
            stage = "CPP"
        elif info["minic"] != 0:
            stats["minic_fail"] += 1
            stage = "MiniC"
        elif info["assembler"] != 0:
            stats["assembler_fail"] += 1
            stage = "GNU_AS"
        else:
            stats["minic_object_pass"] += 1
            stage = "PASS"
        print("BUSYBOX_ALLYES_TU source=%s object=%s stage=%s gcc=%s" %
              (info["source"], info["object"], stage, info["gcc"]))
        if stage != "PASS":
            fname = {"CPP": "cpp.err", "MiniC": "minic.err",
                     "GNU_AS": "as.err"}[stage]
            error = (path.parent / fname).read_text(encoding="utf-8", errors="replace")
            useful = [s.strip() for s in error.splitlines()
                      if "error:" in s or "fatal error:" in s or "unsupported" in s]
            if not useful:
                useful = [s.strip() for s in error.splitlines() if s.strip()]
            print("BUSYBOX_ALLYES_BLOCKER source=%s kind=%s detail=%s" %
                  (info["source"], stage, (useful[0] if useful else "no diagnostic")[:240]))
    total = len(results)
    print("BUSYBOX_ALLYES_FRONTIER=DISCOVERY enabled_symbols=%d attempted_C_TUs=%d "
          "mini_objects=%d cpp_fail=%d minic_fail=%d asm_fail=%d gcc_fail=%d "
          "gcc_full_build=%s full_MiniC_busybox=NOT_TESTED" %
          (enabled, total, stats["minic_object_pass"], stats["cpp_fail"],
           stats["minic_fail"], stats["assembler_fail"], stats["gcc_fail"],
           "PASS" if gcc_success else "FAIL"))
    (report_root / "summary.json").write_text(
        json.dumps({"enabled_symbols": enabled, "kbuild_completed": gcc_success,
                    "attempted_C_TUs": total, **stats}, indent=2, sort_keys=True),
        encoding="utf-8")
    return total


def controller(args):
    root = Path(args.source).resolve()
    out = Path(args.report).resolve()
    out.mkdir(parents=True, exist_ok=True)
    if (out / "per-tu").exists():
        raise SystemExit("refuse stale scan directory: " + str(out / "per-tu"))
    make_env = os.environ.copy()
    for k in ("MAKEFLAGS", "MAKEOVERRIDES", "MFLAGS", "GNUMAKEFLAGS", "CFLAGS"):
        make_env.pop(k, None)
    # Kconfig is the authoritative way to request all possible options.
    with (out / "config.out").open("wb") as stdout, (out / "config.err").open("wb") as stderr:
        rc = subprocess.run(["make", "-j2", "allyesconfig", "CC=" + args.host_cc],
                            cwd=root, env=make_env, stdout=stdout, stderr=stderr,
                            check=False).returncode
    if rc:
        print("BUSYBOX_ALLYES_CONFIG=FAIL stage=original-allyesconfig")
        print((out / "config.err").read_text(errors="replace")[-2500:])
        return 1
    config_path = root / ".config"
    config = config_path.read_text(errors="replace")
    if "CONFIG_WERROR=y" in config:
        # WERROR is a host compiler warning policy, not a BusyBox applet.
        # libselinux marks security_context_t deprecated on current Ubuntu.
        # Keep all Kconfig applets/features requested by allyesconfig while
        # allowing unchanged original BusyBox C to build on this host.
        config_path.write_text(config.replace("CONFIG_WERROR=y",
                                              "# CONFIG_WERROR is not set"),
                               encoding="utf-8")
        with (out / "oldconfig.out").open("wb") as stdout, (out / "oldconfig.err").open("wb") as stderr:
            rc = subprocess.run(["make", "oldconfig", "CC=" + args.host_cc],
                                cwd=root, env=make_env, input=b"\n" * 32,
                                stdout=stdout, stderr=stderr, check=False).returncode
        if rc:
            print("BUSYBOX_ALLYES_CONFIG=FAIL stage=oldconfig-WERROR-policy")
            return 1
        config = config_path.read_text(errors="replace")
        if "CONFIG_WERROR=y" in config:
            print("BUSYBOX_ALLYES_CONFIG=FAIL stage=WERROR-was-not-disabled")
            return 1
        print("BUSYBOX_ALLYES_HOST_WARNING_POLICY=CONFIG_WERROR_n original_sources=unchanged",
              flush=True)
    enabled = len(re.findall(r"^CONFIG_[A-Za-z0-9_]+=y$", config, flags=re.M))
    print("BUSYBOX_ALLYES_CONFIG=PASS mode=original-allyesconfig enabled_symbols=%d" %
          enabled, flush=True)
    make_env["BUSYBOX_ALLYES_MINIC_CC"] = str(Path(args.minic).resolve())
    make_env["BUSYBOX_ALLYES_HOST_CC"] = args.host_cc
    make_env["BUSYBOX_ALLYES_REPORT_ROOT"] = str(out)
    proxy = "python3 " + shlex.quote(str(Path(__file__).resolve())) + " --proxy-cc"
    with (out / "build.out").open("wb") as stdout, (out / "build.err").open("wb") as stderr:
        rc = subprocess.run(["make", "-k", "-j4", "CC=" + proxy, "busybox"],
                            cwd=root, env=make_env, stdout=stdout, stderr=stderr,
                            check=False).returncode
    print("BUSYBOX_ALLYES_GCC_KBUILD=%s original_Kconfig=1 original_sources=1" %
          ("PASS" if rc == 0 else "BLOCKED"), flush=True)
    if rc != 0:
        lines = (out / "build.err").read_text(errors="replace").splitlines()
        print("BUSYBOX_ALLYES_GCC_BUILD_DIAGNOSTIC=" +
              " | ".join(x.strip() for x in lines[-25:])[:1600])
    attempts = print_frontier(out, rc == 0, enabled)
    if attempts == 0:
        print("BUSYBOX_ALLYES_SCAN=FAIL no_real_Kbuild_C_compile_invocations")
        return 1
    print("BUSYBOX_ALLYES_SCAN=PASS diagnostic_only=1 no_MiniC_objects_inserted=1")
    # A red original GCC build or many MiniC failures are meaningful frontier
    # results, not a green full BusyBox certification.
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--proxy-cc":
        return call_proxy(sys.argv[2:])
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--minic", required=True)
    parser.add_argument("--host-cc", default="cc")
    return controller(parser.parse_args())


if __name__ == "__main__":
    sys.exit(main())
