#!/usr/bin/env python3
"""Real original BusyBox Kbuild single-object MiniC substitution oracle.

Requires prior full original GNU build in the *same* pinned BusyBox tree, and
independently GNU-assembled MiniC .o artifacts from the same Kconfig + Kbuild
CC command. Every candidate is compared against the frozen original *whole*
BusyBox binary, not just against an expected textual result. This tool is
opt-in; it never claims all BusyBox objects have been replaced.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


CANDIDATES = (
    ("sed", "editors/sed.o"),
    ("awk", "editors/awk.o"),
    ("find", "findutils/find.o"),
    ("grep", "findutils/grep.o"),
    ("sort", "coreutils/sort.o"),
)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fixtures(report):
    """No nondeterministic clocks, network, shell environment, or file mutations."""
    sample = b"alpha\nbeta\nalpha beta\n"
    return (
        ("sed-n", ("sed", "-n", "2p"), sample),
        ("sed-subst", ("sed", "s/alpha/ALPHA/g"), sample),
        ("awk-field", ("awk", "{print $1}"), sample),
        ("awk-sum", ("awk", "BEGIN { print 7 + 35 }"), b""),
        ("find-depth", ("find", str(report), "-maxdepth", "0", "-type", "d"), b""),
        ("find-name", ("find", str(report), "-maxdepth", "0", "-name", report.name), b""),
        ("grep-F", ("grep", "-F", "beta"), sample),
        ("grep-v", ("grep", "-v", "beta"), sample),
        ("sort", ("sort",), sample),
        ("sort-reverse", ("sort", "-r"), sample),
        ("sh", ("sh", "-c", "printf '%s\n' shell-ok"), b""),
    )


def run_cases(executable, report):
    env = os.environ.copy()
    env["LC_ALL"] = "C"
    results = {}
    for name, argv, input_bytes in fixtures(report):
        try:
            proc = subprocess.run([str(executable), *argv], input=input_bytes,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  cwd=report, env=env, timeout=25, check=False)
            results[name] = (proc.returncode, proc.stdout, proc.stderr)
        except subprocess.TimeoutExpired:
            results[name] = (124, b"", b"timeout")
    return results


def build_with_original_kbuild(root, report, proxy, host_cc, suffix):
    env = os.environ.copy()
    for name in ("MAKEFLAGS", "MAKEOVERRIDES", "MFLAGS", "GNUMAKEFLAGS", "CFLAGS"):
        env.pop(name, None)
    env.update({"BUSYBOX_ALLYES_HOST_CC": host_cc,
                "BUSYBOX_ALLYES_REPORT_ROOT": str(report),
                # Never rebuild the previously qualified shadow inputs while
                # Kbuild only recomputes archive and final executable owners.
                "BUSYBOX_ALLYES_SKIP_SHADOW": "1"})
    stdout = report / (suffix + ".make.out")
    stderr = report / (suffix + ".make.err")
    with stdout.open("wb") as out, stderr.open("wb") as err:
        result = subprocess.run(
            ["make", "-j2", "CC=" + proxy, "busybox"],
            cwd=root, env=env, stdout=out, stderr=err, check=False)
    return result.returncode, stdout, stderr


def invalidate_composite(root, object_path):
    parent = object_path.parent
    # From the original nine-/twelve-object Kbuild mixed validation:
    # delete the containing directory's Kbuild aggregate owners so the
    # already-built MiniC .o must enter the new link.
    for path in (parent / "built-in.o", parent / "lib.a",
                 root / "busybox_unstripped", root / "busybox"):
        if path.exists():
            path.unlink()


def find_candidates(report):
    results = {}
    for result in (report / "per-tu").glob("*/result.json"):
        import json
        row = json.loads(result.read_text(encoding="utf-8"))
        name = row.get("object")
        if name:
            results[name] = (row, result.parent / "mini.o")
    return results


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--proxy", required=True)
    parser.add_argument("--host-cc", default="cc")
    args = parser.parse_args()
    root = Path(args.source).resolve()
    report = Path(args.report).resolve()
    gcc_busybox = root / "busybox"
    if not gcc_busybox.is_file():
        print("BUSYBOX_MIXED_COMPLEX=BLOCKED stage=original-GNU-busybox-missing")
        return 1
    original = report / "gcc-reference-busybox"
    shutil.copy2(gcc_busybox, original)
    oracle = run_cases(original, report)
    print("BUSYBOX_MIXED_COMPLEX_GNU_REFERENCE=PASS "
          "original_full_executable_sha256=%s tests=%d" %
          (sha256(original), len(oracle)), flush=True)
    shadow = find_candidates(report)
    mini_passes = 0
    link_passes = 0
    runtime_passes = 0
    for name, filename in CANDIDATES:
        path = root / filename
        entry = shadow.get(filename)
        if entry is None:
            print("BUSYBOX_MIXED_COMPLEX_APPLET=BLOCKED applet=%s stage=missing-Kbuild-owner" %
                  name, flush=True)
            continue
        info, mini = entry
        if info.get("gcc") != 0 or info.get("cpp") != 0 or info.get("minic") != 0 or info.get("assembler") != 0:
            print("BUSYBOX_MIXED_COMPLEX_APPLET=BLOCKED applet=%s stage=MiniC-GNU_AS " 
                  "gcc=%s cpp=%s minic=%s as=%s" %
                  (name, info.get("gcc"), info.get("cpp"),
                   info.get("minic"), info.get("assembler")), flush=True)
            continue
        if not path.is_file() or not mini.is_file():
            print("BUSYBOX_MIXED_COMPLEX_APPLET=BLOCKED applet=%s stage=missing-real-object" %
                  name, flush=True)
            continue
        mini_passes += 1
        baseline_object = report / ("gcc-original-" + name + ".o")
        shutil.copy2(path, baseline_object)
        if sha256(baseline_object) == sha256(mini):
            print("BUSYBOX_MIXED_COMPLEX_APPLET=BLOCKED applet=%s stage=identical-object"
                  % name, flush=True)
            continue
        # Every candidate is tested independently against all-original-GNU
        # objects, so one miscompiled applet cannot contaminate another's
        # verdict. Do not alter upstream application sources, ever.
        shutil.copy2(mini, path)
        invalidate_composite(root, path)
        code, stdout, stderr = build_with_original_kbuild(
            root, report, args.proxy, args.host_cc, name)
        if code != 0 or not gcc_busybox.is_file():
            err = stderr.read_text(encoding="utf-8", errors="replace")
            print("BUSYBOX_MIXED_COMPLEX_APPLET=BLOCKED applet=%s stage=GNU-Kbuild-link "
                  "exit=%d detail=%r" % (name, code, err[-850:]), flush=True)
        elif sha256(gcc_busybox) == sha256(original):
            print("BUSYBOX_MIXED_COMPLEX_APPLET=BLOCKED applet=%s stage=linked-binary-unchanged" %
                  name, flush=True)
        else:
            link_passes += 1
            current = run_cases(gcc_busybox, report)
            differences = [key for key, value in oracle.items()
                           if current.get(key) != value]
            if differences:
                for key in differences[:3]:
                    print("BUSYBOX_MIXED_COMPLEX_DIFF applet=%s fixture=%s gcc=%r mixed=%r" %
                          (name, key, oracle[key], current.get(key)), flush=True)
                print("BUSYBOX_MIXED_COMPLEX_APPLET=BLOCKED applet=%s "
                      "stage=runtime-byte-differential fixtures_failed=%d" %
                      (name, len(differences)), flush=True)
            else:
                runtime_passes += 1
                print("BUSYBOX_MIXED_COMPLEX_APPLET=PASS applet=%s "
                      "MiniC_object=1 original_GNU_Kbuild_link=1 GCC_11_fixture_byte_exact=1 "
                      "binary_changed=1" % name, flush=True)
        # Restore object and Kbuild aggregates before trying next owner.
        shutil.copy2(baseline_object, path)
        invalidate_composite(root, path)
        restore_code, _, restore_err = build_with_original_kbuild(
            root, report, args.proxy, args.host_cc, "restore-" + name)
        if restore_code != 0 or not gcc_busybox.is_file() or sha256(gcc_busybox) != sha256(original):
            print("BUSYBOX_MIXED_COMPLEX=FAIL stage=unable-to-restore-GNU-oracle "
                  "applet=%s exit=%d detail=%r" %
                  (name, restore_code, restore_err.read_text(errors="replace")[-500:]),
                  flush=True)
            return 1
    print("BUSYBOX_MIXED_COMPLEX_FRONTIER=DISCOVERY candidates=%d mini_objects=%d "
          "GNU_links=%d runtime_exact=%d fixtures_per_applet=%d "
          "original_GNU_oracle_sha256=%s" %
          (len(CANDIDATES), mini_passes, link_passes, runtime_passes,
           len(oracle), sha256(original)), flush=True)
    if runtime_passes == len(CANDIDATES):
        print("BUSYBOX_MIXED_COMPLEX=PASS all_five_independent=1 "
              "GCC_original_oracle=1 real_Kbuild_link=1", flush=True)
        return 0
    print("BUSYBOX_MIXED_COMPLEX=BLOCKED passed=%d total=%d" %
          (runtime_passes, len(CANDIDATES)), flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
