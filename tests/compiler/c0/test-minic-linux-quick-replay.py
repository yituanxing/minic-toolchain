#!/usr/bin/env python3
"""Synthetic frozen Linux .i archive checks for quick-replay (no full Kbuild)."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[3]
TOOL = ROOT / "tools/dev/minic-linux-quick-replay.py"


def load_tool():
    spec = importlib.util.spec_from_file_location("minic_linux_quick_replay_test", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def corpus(path, arch, rows):
    manifest = {"schema": "three-isa-gnu-i-v1", "identity": {"target": arch},
                "errors": [], "rows": []}
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for i, (name, source, flags, as_flags) in enumerate(rows):
            filename = "i/" + name[:-2] + ".i"
            content = source.encode("ascii")
            manifest["rows"].append({"object": name, "input": filename,
                                     "index": i, "minic_flags": flags,
                                     "gnu_as_flags": as_flags,
                                     "sha256": hashlib.sha256(content).hexdigest()})
            output.writestr("frozen/" + filename, content)
        output.writestr("frozen/manifest.json", json.dumps(manifest))


def invoke(archive, compiler, output, owners, extra=()):
    command = [sys.executable, str(TOOL), "--corpus", "x86_64=" + str(archive),
               "--minic", str(compiler), "--output", str(output),
               "--workers", "2", "--timeout", "45"]
    for owner in owners:
        command.extend(("--owner", "x86_64:" + owner))
    command.extend(extra)
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        raise AssertionError("quick replay error:\n" + result.stdout + result.stderr)
    return json.loads(output.read_text())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--minic", type=Path, default=ROOT / "build/release/bin/minic-cc")
    args = p.parse_args()
    compiler = args.minic.resolve(strict=True)
    tool = load_tool()
    x86_flags = ["-fshort-wchar", "-funsigned-char", "-fno-pie",
                 "-fcf-protection=branch", "-mindirect-branch=thunk-extern",
                 "-mindirect-branch-register", "-mfunction-return=thunk-extern",
                 "-fpatchable-function-entry=16,16"]
    source_bad = ("extern void __compiletime_assert_987(void);\n"
                  "extern void other_unresolved_owner(void);\n"
                  "void linux_owner(void) { __compiletime_assert_987(); "
                  "other_unresolved_owner(); }\n")
    source_good = ("extern void __compiletime_assert_987(void);\n"
                   "void linux_owner_ok(void) { if (0) __compiletime_assert_987(); }\n")
    with tempfile.TemporaryDirectory(prefix="minic-quick-gnu-i-test-") as scratch:
        temp = Path(scratch)
        archives = {arch: temp / (arch + ".zip")
                    for arch in ("x86_64", "aarch64", "loongarch64")}
        corpus(archives["x86_64"], "x86_64", [
            ("kernel/owner_bad.o", source_bad, x86_flags, ["-m64"]),
            ("kernel/owner_good.o", source_good, x86_flags, ["-m64"])])
        corpus(archives["aarch64"], "aarch64", [
            ("kernel/owner.o", source_bad, ["-fshort-wchar"], [])])
        corpus(archives["loongarch64"], "loongarch64", [
            ("kernel/owner.o", source_bad, ["-fshort-wchar", "-mabi=lp64s"],
             ["-mabi=lp64s"])])
        for arch, archive in archives.items():
            rows = tool.load(archive, arch, set(), 1, assemble=True)
            if len(rows) != 1 or rows[0][0] != arch:
                raise AssertionError("lost SHA-verified frozen owner for " + arch)

        # A GNU artifact is untrusted input, including its forwarding flags.
        for arch, bad_flag in (("x86_64", "-fplugin=/tmp/evil.so"),
                               ("aarch64", "-mindirect-branch=thunk-extern"),
                               ("loongarch64", "-mabi=unknown")):
            evil = temp / (arch + "-evil.zip")
            corpus(evil, arch, [("kernel/evil.o", source_bad, [bad_flag], [])])
            try:
                tool.load(evil, arch, set(), 1)
            except ValueError:
                pass
            else:
                raise AssertionError("accepted foreign/evil GNU flag for " + arch)
        as_evil = temp / "x86-as-evil.zip"
        corpus(as_evil, "x86_64", [("kernel/evil.o", source_bad, x86_flags,
                                  ["-m64\n-o /tmp/escape.o"])])
        try:
            tool.load(as_evil, "x86_64", set(), 1, assemble=True)
        except ValueError:
            pass
        else:
            raise AssertionError("accepted injected GNU assembler flag")

        owners = ("kernel/owner_bad.o", "kernel/owner_good.o")
        source_only = invoke(archives["x86_64"], compiler,
                             temp / "source-only.json", owners)
        if {row["after"]["status"] for row in source_only["results"]} != {"S_PASS"}:
            raise AssertionError("broken source-only quick replay")
        linked = invoke(archives["x86_64"], compiler,
                        temp / "gnu-as-nm.json", owners,
                        ("--gnu-as", "--gnu-cc", "x86_64=gcc",
                         "--gnu-nm", "x86_64=nm", "--before", str(compiler)))
        if linked["verdict"] != "NO_FULL_GNU_LD_QEMU":
            raise AssertionError("incorrect full-link certification claim")
        results = {row["owner"]: row for row in linked["results"]}
        if set(results) != set(owners):
            raise AssertionError("missing differential GNU AS owner")
        for owner in owners:
            row = results[owner]
            if (row["after"]["status"] != "S_GNU_AS_PASS" or
                    row["before"]["status"] != "S_GNU_AS_PASS" or
                    row["delta"] != "UNCHANGED"):
                raise AssertionError("GNU AS differential failure: " + owner)
        if results[owners[0]]["after"]["assert_symbols"] != ["__compiletime_assert_987"]:
            raise AssertionError("GNU nm missed authentic unresolved Linux guard")
        if not {"__compiletime_assert_987", "other_unresolved_owner"}.issubset(
                results[owners[0]]["after"]["undefined_symbols"]):
            raise AssertionError("GNU nm failed to retain all unresolved symbols: " +
                                 repr(results[owners[0]]["after"]["undefined_symbols"]))
        if results[owners[1]]["after"]["assert_symbols"]:
            raise AssertionError("GNU nm reported dead compiletime guard")
        # Independent GCC -O2 reference with the *same* GNU-oriented C input:
        # a live external assert survives, a dead one cannot be invented.
        for index, source in enumerate((source_bad, source_good)):
            c_input = temp / ("gnu-original-%d.c" % index)
            assembly = temp / ("gnu-original-%d.s" % index)
            obj = temp / ("gnu-original-%d.o" % index)
            c_input.write_text(source)
            subprocess.run(["gcc", *x86_flags, "-O2", "-S", "-x", "c",
                            str(c_input), "-o", str(assembly)], check=True,
                           stdout=subprocess.DEVNULL)
            subprocess.run(["gcc", "-m64", "-x", "assembler", "-c",
                            str(assembly), "-o", str(obj)], check=True,
                           stdout=subprocess.DEVNULL)
            undefined = subprocess.check_output(["nm", "-u", str(obj)], text=True)
            expected = index == 0
            if ("__compiletime_assert_987" in undefined) != expected:
                raise AssertionError("GNU -O2 reference disagreed with live/dead assert")

    print("MINIC_LINUX_QUICK_REPLAY=PASS x86_thunk=1 aarch_short_wchar=1 "
          "loong_lp64s=1 evil_rejected=4 x86_gnu_as_nm=2 gcc_o2_reference=2 "
          "strict_link_claim=0")


if __name__ == "__main__":
    main()
