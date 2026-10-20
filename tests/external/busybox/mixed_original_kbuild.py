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
import json
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
            qemu = os.environ.get("BUSYBOX_ALLYES_QEMU")
            sysroot = os.environ.get("BUSYBOX_ALLYES_SYSROOT", "")
            launch = [qemu, "-L", sysroot] if qemu else []
            proc = subprocess.run([*launch, str(executable), *argv], input=input_bytes,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  cwd=report, env=env, timeout=25, check=False)
            results[name] = (proc.returncode, proc.stdout, proc.stderr)
        except subprocess.TimeoutExpired:
            results[name] = (124, b"", b"timeout")
    return results


def build_with_original_kbuild(root, report, proxy, host_cc, suffix,
                              owner=None, mini_path=None, replace_map=None):
    env = os.environ.copy()
    for name in ("MAKEFLAGS", "MAKEOVERRIDES", "MFLAGS", "GNUMAKEFLAGS", "CFLAGS"):
        env.pop(name, None)
    env.update({"BUSYBOX_ALLYES_HOST_CC": host_cc,
                "BUSYBOX_ALLYES_REPORT_ROOT": str(report),
                # Never rebuild independent shadow inputs on the relink.
                "BUSYBOX_ALLYES_SKIP_SHADOW": "1"})
    if owner is not None:
        if mini_path is None:
            raise ValueError("MiniC substitution requires its object")
        env["BUSYBOX_ALLYES_REPLACE_OWNER"] = str(owner)
        env["BUSYBOX_ALLYES_REPLACE_MINIC_OBJECT"] = str(mini_path)
        env["BUSYBOX_ALLYES_REPLACE_EVENT"] = str(report / (suffix + ".replace.txt"))
    if replace_map is not None:
        if owner is not None:
            raise ValueError("cannot combine single-owner and cohort routes")
        env["BUSYBOX_ALLYES_REPLACE_MAP"] = str(replace_map)
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
    # BusyBox dispatches on argv[0]: the executable MUST be named "busybox"
    # for explicit 'busybox sed ...' semantics. The first run showed that
    # naming this reference 'gcc-reference-busybox' falsely made all eleven
    # GNU applets return 127 while mixed correctly executed the applets.
    original = report / "gcc-reference" / "busybox"
    original.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(gcc_busybox, original)
    oracle = run_cases(original, report)
    expected_basic = {"sed-n": b"beta\n", "awk-sum": b"42\n",
                      "grep-F": b"beta\nalpha beta\n",
                      "sort": b"alpha\nalpha beta\nbeta\n"}
    if any(oracle.get(key) != (0, output, b"")
           for key, output in expected_basic.items()) or any(
               status != 0 for status, _, _ in oracle.values()):
        print("BUSYBOX_MIXED_COMPLEX_GNU_REFERENCE=FAIL reason=applet-dispatch"
              " verdicts=%r" % oracle, flush=True)
        return 1
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
        # Remove the original GCC object, deliberately making Kbuild perform
        # a *real* source-owner CC invocation. The exact CC proxy compiles the
        # unmodified source with GCC and substitutes MiniC .o at that owner
        # boundary, before Kbuild archives and links the resulting object.
        # Earlier blind cp-then-relink was correctly caught as false ownership:
        # Kbuild recompiled and overwrote the copied MiniC .o.
        path.unlink()
        invalidate_composite(root, path)
        code, stdout, stderr = build_with_original_kbuild(
            root, report, args.proxy, args.host_cc, name, owner=path,
            mini_path=mini)
        replaced_event = report / (name + ".replace.txt")
        if code != 0 or not gcc_busybox.is_file():
            err = stderr.read_text(encoding="utf-8", errors="replace")
            print("BUSYBOX_MIXED_COMPLEX_APPLET=BLOCKED applet=%s stage=GNU-Kbuild-link "
                  "exit=%d detail=%r" % (name, code, err[-850:]), flush=True)
        elif not replaced_event.is_file() or sha256(path) != sha256(mini):
            print("BUSYBOX_MIXED_COMPLEX_APPLET=BLOCKED applet=%s "
                  "stage=Kbuild-owner-replacement-not-retained event=%d checksum_ok=%d" %
                  (name, replaced_event.is_file(), sha256(path) == sha256(mini)),
                  flush=True)
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
        if path.exists():
            path.unlink()
        invalidate_composite(root, path)
        restore_code, _, restore_err = build_with_original_kbuild(
            root, report, args.proxy, args.host_cc, "restore-" + name)
        if restore_code != 0 or not gcc_busybox.is_file() or not path.is_file() or sha256(path) == sha256(mini):
            print("BUSYBOX_MIXED_COMPLEX=FAIL stage=unable-to-restore-GNU-object "
                  "applet=%s exit=%d detail=%r" %
                  (name, restore_code, restore_err.read_text(errors="replace")[-500:]),
                  flush=True)
            return 1
        restored = run_cases(gcc_busybox, report)
        if restored != oracle:
            differences = [key for key, value in oracle.items() if restored.get(key) != value]
            print("BUSYBOX_MIXED_COMPLEX=FAIL stage=restored-GNU-runtime-not-byte-exact "
                  "applet=%s differing=%s" % (name, differences), flush=True)
            return 1
        if sha256(gcc_busybox) != sha256(original):
            print("BUSYBOX_MIXED_COMPLEX_GNU_REBUILD_NOTE applet=%s "
                  "binary_sha_changes_on_relink=1 object_and_runtime_verified=1" %
                  name, flush=True)
    print("BUSYBOX_MIXED_COMPLEX_FRONTIER=DISCOVERY candidates=%d mini_objects=%d "
          "GNU_links=%d runtime_exact=%d fixtures_per_applet=%d "
          "original_GNU_oracle_sha256=%s" %
          (len(CANDIDATES), mini_passes, link_passes, runtime_passes,
           len(oracle), sha256(original)), flush=True)
    if runtime_passes != len(CANDIDATES):
        print("BUSYBOX_MIXED_COMPLEX=BLOCKED passed=%d total=%d" %
              (runtime_passes, len(CANDIDATES)), flush=True)
        return 1

    # The five independent substitutions did not test that their object
    # contributions coexist. Make a sixth REAL Kbuild binary with all five
    # MiniC source owners injected together. The prior restore passes left
    # the GNU-only Kbuild and eleven-fixture oracle fully verified.
    replacements = {}
    for name, filename in CANDIDATES:
        owner = root / filename
        info, mini = shadow[filename]
        if not owner.is_file() or not mini.is_file():
            print("BUSYBOX_MIXED_COMPLEX_COMBINED=BLOCKED stage=missing-owner applet=%s" %
                  name, flush=True)
            return 1
        replacements[str(owner.resolve())] = str(mini.resolve())
    mapping = report / "combined-five-owners.json"
    mapping.write_text(json.dumps(replacements, sort_keys=True, indent=2),
                       encoding="utf-8")
    for name, filename in CANDIDATES:
        owner = root / filename
        owner.unlink()
        invalidate_composite(root, owner)
    code, _, err = build_with_original_kbuild(
        root, report, args.proxy, args.host_cc, "combined-five",
        replace_map=mapping)
    if code != 0 or not gcc_busybox.is_file():
        print("BUSYBOX_MIXED_COMPLEX_COMBINED=BLOCKED stage=Kbuild-link exit=%d "
              "details=%r" % (code, err.read_text(errors="replace")[-1200:]),
              flush=True)
        return 1
    events = report / "combined-events"
    for name, filename in CANDIDATES:
        owner = root / filename
        mini = Path(replacements[str(owner.resolve())])
        event = events / (name + ".replace.txt")
        if not owner.is_file() or not event.is_file() or sha256(owner) != sha256(mini):
            print("BUSYBOX_MIXED_COMPLEX_COMBINED=BLOCKED "
                  "stage=Kbuild-owner-not-MiniC applet=%s event=%d" %
                  (name, event.is_file()), flush=True)
            return 1
    if sha256(gcc_busybox) == sha256(original):
        print("BUSYBOX_MIXED_COMPLEX_COMBINED=BLOCKED stage=linked-GNU-reference-unchanged",
              flush=True)
        return 1
    combined = run_cases(gcc_busybox, report)
    diffs = [key for key, value in oracle.items() if combined.get(key) != value]
    if diffs:
        for key in diffs[:5]:
            print("BUSYBOX_MIXED_COMPLEX_COMBINED_DIFF fixture=%s gcc=%r mixed=%r" %
                  (key, oracle[key], combined.get(key)), flush=True)
        print("BUSYBOX_MIXED_COMPLEX_COMBINED=BLOCKED stage=runtime-byte-differential "
              "fixtures_failed=%d" % len(diffs), flush=True)
        return 1
    print("BUSYBOX_MIXED_COMPLEX_COMBINED=PASS MiniC_owners=5 "
          "real_GNU_Kbuild_link=1 independent_GNU_oracle=1 "
          "eleven_fixture_byte_exact=1 sha_changed=1", flush=True)
    print("BUSYBOX_MIXED_COMPLEX=PASS all_five_independent=1 "
          "combined_five=1 GCC_original_oracle=1 real_Kbuild_link=1",
          flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
