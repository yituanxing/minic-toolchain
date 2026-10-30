#!/usr/bin/env python3
"""Exact-source all-C MiniC kernel producer for GNU Linux 6.6.143.

Requires an independently QEMU-accepted GNU kernel on this SAME Kbuild tree.
Every authentic ELF64 GNU .o.cmd C owner must pass frozen GNU -E -> MiniC -S
-> target GNU AS.  The resulting .o files replace ALL GNU C objects before
the original Kbuild links a new image.  Post-link object hashes must remain
unchanged; even one original GNU C fallback rejects the whole campaign.

This script is NOT a standalone runtime certification: it refuses to issue a
MiniC PASS unless both real P0/P1 QEMU boots and the independent GNU/MiniC
semantic differential complete successfully.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import functools
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import time


LINUX_SHA = "dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932"
TARGETS = {
    "x86_64": ("x86", "", "gcc", "bzImage", "arch/x86/boot/bzImage", 62),
    "aarch64": ("arm64", "aarch64-linux-gnu-", "aarch64-linux-gnu-gcc", "Image", "arch/arm64/boot/Image", 183),
    "loongarch64": ("loongarch", "loongarch64-linux-gnu-", "loongarch64-linux-gnu-gcc-13", "vmlinuz.efi", "arch/loongarch/boot/vmlinuz.efi", 258),
}
ALLOWED_CFLAGS = {
    "-fshort-wchar", "-fno-short-wchar", "-fsigned-char", "-funsigned-char",
    "-fno-signed-char", "-fno-unsigned-char", "-ffunction-sections",
    "-fno-function-sections", "-fPIE", "-fpie", "-fPIC", "-fpic",
    "-fno-pie", "-fno-PIE", "-fno-pic", "-fno-PIC",
}
LOONGARCH_ABI_FLAGS = {"-mabi=lp64s", "-mabi=lp64d"}
GNU_VERDICT = "GNU_QEMU_P0_P1_PASS_NOT_MINIC"
MINIC_VERDICT = "MINIC_QEMU_P0_P1_PASS_NO_GCC_C_FALLBACK"


class Rejected(ValueError):
    pass


def sha(path: Path) -> str:
    hash_ = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1048576), b""):
            hash_.update(chunk)
    return hash_.hexdigest()


def required(path: Path) -> Path:
    if not path.is_file() or path.is_symlink() or path.stat().st_size == 0:
        raise Rejected(f"missing/unsafe authentic input: {path}")
    return path


def module(name: str, filename: str):
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise Rejected(f"missing strict source oracle: {path}")
    imported = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(imported)
    return imported


@functools.lru_cache(maxsize=1)
def frozen_kbuild_oracle():
    return module("minic_gnu_assembler_flags", "linux-three-isa-freeze-i-v1.py")


def compiler_pair(compiler: Path, expected: dict | None = None) -> dict[str, str]:
    """Authenticate both the front driver and the *executed* sibling compiler."""
    driver = required(compiler)
    cc = required(compiler.parent / "minic-cc")
    if (not os.access(driver, os.X_OK) or not os.access(cc, os.X_OK)
            or cc.parent != driver.parent):
        raise Rejected("MiniC driver and sibling minic-cc must both be executable")
    identity = {"compiler_sha256": sha(driver),
                "compiler_driver_sha256": sha(driver),
                "compiler_cc_sha256": sha(cc)}
    if expected is not None and any(expected.get(key) != value
                                    for key, value in identity.items()):
        raise Rejected("MiniC driver or executed minic-cc binary changed during compilation")
    return identity


def verify_gnu_kbuild_commands(out: Path, rows: list[dict], gcc: str) -> None:
    """Ensure canonical GNU .cmd source/flags identity never changed."""
    for row in rows:
        name = PurePosixPath(row["object"])
        cmd = out / name.parent / ("." + name.name + ".cmd")
        digest = row.get("cmd_sha256")
        if (not isinstance(digest, str) or
                not re.fullmatch(r"[0-9a-f]{64}", digest) or
                sha(required(cmd)) != digest):
            raise Rejected(f"original GNU Kbuild .cmd provenance changed: {name}")
        try:
            _, _, authentic_as_flags, _ = frozen_kbuild_oracle().gnu_argv(
                cmd.read_text(), gcc, name.as_posix(), Path("unused.i"))
        except (OSError, ValueError) as exc:
            raise Rejected(f"invalid authentic GNU machine options for {name}: {exc}") from exc
        if row.get("gnu_as_flags") != authentic_as_flags:
            raise Rejected(f"original GNU AS machine options changed: {name}")


def gnu_assembler_command(gcc: str, row: dict, asm: Path, obj: Path) -> list[str]:
    """Keep the *original C owner's* GNU machine ABI when assembling MiniC -S.

    The original GNU -E input cannot retain -mabi, -mcmodel or -Wa flags.
    Linux's LoongArch kernel uses lp64s while distro GCC defaults to lp64d;
    silently dropping -mabi would make the whole VDSO unlinkable.
    """
    flags = row.get("gnu_as_flags")
    if not frozen_kbuild_oracle().valid_assembler_flags(flags):
        raise Rejected(f"invalid original GNU AS machine flags: {row['object']}")
    return [gcc, *flags, "-x", "assembler", "-c", str(asm), "-o", str(obj)]


def validate_original_abi(obj: Path, target: str, row: dict) -> None:
    """Enforce matching LoongArch ELF ABI against the frozen GNU C owner."""
    if target != "loongarch64":
        return
    expected = row.get("gnu_elf_abi")
    if expected not in (1, 2, 3):
        raise Rejected(f"missing canonical LoongArch64 GNU ELF ABI: {row['object']}")
    with obj.open("rb") as stream:
        hdr = stream.read(64)
    actual = struct.unpack_from("<I", hdr, 48)[0] & 7
    if actual != expected:
        raise Rejected(f"MiniC GNU AS LoongArch64 ABI mismatch: {row['object']} "
                       f"original={expected} candidate={actual}")


def candidate_inputs(target: str, work: Path, compiler: Path,
                     golden_receipt: dict, initramfs: Path) -> tuple[list[dict], dict]:
    evidence, out, frozen = work / "evidence", work / "build", work / "frozen"
    arch, cross, gcc, build_target, image_name, machine = TARGETS[target]
    if golden_receipt.get("verdict") != GNU_VERDICT or golden_receipt.get("target") != target:
        raise Rejected("a real independently accepted same-target GNU P0/P1 receipt is required")
    if (golden_receipt.get("source_sha256") != LINUX_SHA or
            sha(required(work / "linux-6.6.143.tar.xz")) != LINUX_SHA or
            sha(required(out / ".config")) != golden_receipt.get("config_sha256") or
            sha(required(initramfs)) != golden_receipt.get("initramfs_sha256")):
        raise Rejected("GNU/MiniC source, config or initramfs SHA changed")
    source = required(work / "linux-6.6.143" / "Makefile")
    if "6" not in source.read_text()[:100]:
        raise Rejected("original Linux version Makefile missing")
    gnu_image = required(out / image_name)
    if sha(gnu_image) != golden_receipt.get("image_sha256"):
        raise Rejected("the original GNU boot Image was altered before MiniC replacement")
    discovery = json.loads(required(evidence / "discovery.json").read_text())
    if (discovery.get("verdict") != "GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC" or
            discovery.get("target") != target or
            discovery.get("config_profile") != "broad" or
            discovery.get("config_sha256") != golden_receipt.get("config_sha256") or
            discovery.get("gnu_c_owners") != golden_receipt.get("gnu_c_owners") or
            discovery.get("owner_manifest_sha256") != golden_receipt.get("gnu_c_owner_manifest_sha256")):
        raise Rejected("GNU discovery and QEMU receipt have different original C owner identities")
    oracle = module("minic_freeze", "linux-three-isa-freeze-i-v1.py")
    oracle.verify(evidence, frozen, target, gcc)
    freeze = json.loads(required(frozen / "manifest.json").read_text())
    rows = freeze.get("rows", [])
    if (not isinstance(rows, list) or len(rows) < 200 or
            len(rows) != discovery["gnu_c_owners"] or
            len({x.get("object") for x in rows}) != len(rows)):
        raise Rejected("incomplete canonical Linux ELF64 GNU C-owner manifest")
    if freeze.get("identity", {}).get("profile") != "broad":
        raise Rejected("GNU frozen profile must match the full broad Kconfig")
    verify_gnu_kbuild_commands(out, rows, gcc)
    for row in rows:
        name = PurePosixPath(row["object"])
        allowed = ALLOWED_CFLAGS | (LOONGARCH_ABI_FLAGS if target == "loongarch64" else set())
        if (name.is_absolute() or ".." in name.parts or name.suffix != ".o" or
                not isinstance(row.get("minic_flags"), list) or
                any(flag not in allowed for flag in row["minic_flags"]) or
                not isinstance(row.get("gnu_as_flags"), list)):
            raise Rejected(f"unsafe or unsupported owner: {row['object']}")
        required(out / name)
        if not (out / name).is_file():
            raise Rejected(f"missing GNU original C object: {name}")
    tool_identity = compiler_pair(compiler)
    return rows, {"target": target, "gnu_c_owners": len(rows),
                  "golden_image_sha256": golden_receipt["image_sha256"],
                  "source_sha256": LINUX_SHA,
                  "config_sha256": golden_receipt["config_sha256"],
                  "initramfs_sha256": golden_receipt["initramfs_sha256"],
                  "firmware_sha256": golden_receipt.get("firmware_sha256"),
                  "gnu_c_owner_manifest_sha256": discovery["owner_manifest_sha256"],
                  **tool_identity, "arch": arch, "cross": cross,
                  "gcc": gcc, "build_target": build_target,
                  "image_name": image_name, "machine": machine}


def elf_machine(path: Path, machine: int) -> None:
    with path.open("rb") as stream:
        head = stream.read(64)
    if (len(head) < 64 or head[:4] != b"\x7fELF" or
            head[4] != 2 or head[5] != 1 or
            struct.unpack_from("<H", head, 18)[0] != machine or
            struct.unpack_from("<H", head, 16)[0] != 1):
        raise Rejected(f"not a relocatable target ELF64 owner: {path}")


def make_objects(rows: list[dict], work: Path, compiler: Path, output: Path,
                 data: dict, workers: int, timeout: int) -> list[dict]:
    frozen, staging = work / "frozen", output / "staged"
    staging.mkdir(parents=True, exist_ok=False)
    gcc = data["gcc"]

    def one(row: dict) -> dict:
        name = PurePosixPath(row["object"])
        source = frozen / row["input"]
        asm, obj = staging / name.with_suffix(".s"), staging / name
        failure_stderr = staging / name.with_suffix(".tool-stderr.txt")
        asm.parent.mkdir(parents=True, exist_ok=True)
        result = dict(object=str(name), input_sha256=row["sha256"],
                      status="MINIC_AS_NOT_RUN", asm_sha256=None, obj_sha256=None)
        if sha(required(source)) != row["sha256"]:
            raise Rejected(f"frozen input changed: {name}")
        started = time.monotonic()
        try:
            command = [str(compiler), f"--target={data['target']}-linux",
                       *row["minic_flags"], "-S", str(source), "-o", str(asm)]
            cc = subprocess.run(command, text=True, capture_output=True, timeout=timeout)
            if cc.returncode or not asm.is_file() or not asm.stat().st_size:
                failure_stderr.write_text(cc.stderr)
                result.update(status="MINIC_S_FAIL", detail=cc.stderr[-600:])
                return result
            result["asm_sha256"] = sha(asm)
            # Original GNU Kbuild invokes the assembler from its O= build
            # directory. `.incbin "kernel/config_data.gz"` and other genuine
            # relative assembly inputs must resolve against THAT directory,
            # not this CI tool's repository checkout working directory.
            ass = subprocess.run(gnu_assembler_command(gcc, row, asm, obj),
                                 cwd=work / "build", text=True,
                                 capture_output=True, timeout=timeout)
            if ass.returncode or not obj.is_file() or not obj.stat().st_size:
                failure_stderr.write_text(ass.stderr)
                result.update(status="GNU_TARGET_AS_FAIL", detail=ass.stderr[-600:])
                return result
            elf_machine(obj, data["machine"])
            validate_original_abi(obj, data["target"], row)
            result.update(status="MINIC_S_GNU_AS_PASS", obj_sha256=sha(obj))
            return result
        except (OSError, subprocess.TimeoutExpired, Rejected) as exc:
            failure_stderr.write_text(f"{type(exc).__name__}: {exc}\n")
            result.update(status="SOURCE_AS_ERROR", detail=str(exc)[-600:])
            return result
        finally:
            result["elapsed_seconds"] = round(time.monotonic() - started, 3)

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(one, rows))
    return results


def preserve_first_failed_owners(work: Path, output: Path, rows: list[dict],
                                 results: list[dict], limit: int = 8) -> dict:
    """Retain bounded *original* GNU -E and exact .o.cmd failure reproductions.

    Failed owner inputs are never swapped with GNU compiler output and are
    never retried during the campaign. This only copies first-error evidence
    before the existing fail-closed no-partial-C-owner gate rejects the run.
    """
    failures = [item for item in results if item["status"] != "MINIC_S_GNU_AS_PASS"]
    if not failures:
        raise Rejected("failed-owner diagnostics require at least one failed owner")
    if not 1 <= limit <= 8:
        raise Rejected("first-error diagnostic budget must remain 1..8")
    by_name = {item["object"]: item for item in rows}
    samples = output / "failed-owner-diagnostics"
    samples.mkdir(parents=True, exist_ok=False)
    report = {"schema": "minic-three-isa-first-failed-owners-v1",
              "total_failed_owners": len(failures), "sample_limit": limit,
              "sample_count": 0, "diagnostics_only": True,
              "no_gcc_retry_or_fallback": True, "samples": [],
              "diagnostic_errors": []}
    for failed in failures[:limit]:
        name = PurePosixPath(failed["object"])
        folder = samples / name.with_suffix("")
        folder.mkdir(parents=True, exist_ok=False)
        entry = {"owner": str(name), "failure_status": failed["status"],
                 "error_tail": failed.get("detail", ""), "files": {}}
        row = by_name.get(str(name))
        if row is None:
            report["diagnostic_errors"].append(f"{name}:not in frozen GNU owner manifest")
            report["samples"].append(entry)
            continue
        sources = {
            "original.gnu.i": work / "frozen" / row["input"],
            "original.gnu.o.cmd": work / "build" / name.parent / ("." + name.name + ".cmd"),
            "original.gnu.o": work / "build" / name,
            "minic.s": output / "staged" / name.with_suffix(".s"),
            "minic.o": output / "staged" / name,
            "tool-stderr.txt": output / "staged" / name.with_suffix(".tool-stderr.txt"),
        }
        for label, source in sources.items():
            try:
                if not source.is_file() or source.is_symlink():
                    if label in ("minic.s", "minic.o"):
                        entry["files"][label] = {"status": "NOT_PRODUCED"}
                        continue
                    raise Rejected(f"missing authentic first-error evidence: {source}")
                destination = folder / label
                shutil.copyfile(source, destination)
                digest = sha(destination)
                entry["files"][label] = {"status": "COPIED", "sha256": digest,
                                         "bytes": destination.stat().st_size}
                if label == "original.gnu.i" and digest != row["sha256"]:
                    raise Rejected(f"frozen GNU C input changed while preserving {name}")
                if label == "original.gnu.o.cmd" and digest != row["cmd_sha256"]:
                    raise Rejected(f"original GNU Kbuild .cmd changed while preserving {name}")
            except (OSError, Rejected) as exc:
                entry["files"][label] = {"status": "DIAGNOSTIC_ERROR", "error": str(exc)}
                report["diagnostic_errors"].append(f"{name}:{label}:{exc}")
        report["samples"].append(entry)
    report["sample_count"] = len(report["samples"])
    report["verdict"] = ("FIRST_FAILED_OWNER_DIAGNOSTICS_COMPLETE" if
                         not report["diagnostic_errors"] else
                         "FIRST_FAILED_OWNER_DIAGNOSTICS_PARTIAL")
    (samples / "index.json").write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print(f"THREE_ISA_FAILED_OWNER_DIAGNOSTIC verdict={report['verdict']} "
          f"captured={report['sample_count']}/{len(failures)} limit={limit}")
    return report


def verify_postlink_c_owners(out: Path, output: Path, target: str,
                             expected: dict[str, str]) -> dict:
    """Re-enumerate live GNU Kbuild ELF64 C owners after the final relink.

    Checking only the previously known MiniC filenames misses a newly emitted
    original GNU C object. Reuse the original canonical .o.cmd C-source
    classifier on the COMPLETE postlink target-ELF64 object inventory.
    """
    original = module("minic_original_owner_census", "linux-three-isa-golden-v1.py")
    common = module("minic_original_full_c", "linux-runtime-gcc-full-c-objects-v1.py")
    out = out.resolve(strict=True)
    records: list[tuple[str, str]] = []
    for obj in sorted(out.rglob("*.o")):
        if not obj.is_file():
            continue
        parts = obj.relative_to(out).parts
        if (parts and parts[0] in ("tools", "scripts") or
                len(parts) >= 3 and parts[0] == "arch" and parts[2] == "tools"):
            continue
        if original.classify_object(obj, out, target) != "main":
            continue
        records.append((obj.relative_to(out).as_posix(), sha(obj)))
    manifest = output / "postlink-target-elf64-objects.sha256"
    manifest.write_text("".join(f"{digest}  {name}\n" for name, digest in records))
    owners, _excluded = common.examine(out, manifest)
    actual = set(owners)
    wanted = set(expected)
    unexpected = sorted(actual - wanted)
    missing = sorted(wanted - actual)
    report = {"schema": "minic-three-isa-postlink-c-owners-v1",
              "target": target, "expected_c_owners": len(expected),
              "actual_c_owners": len(actual), "unexpected_gnu_c_owners": unexpected,
              "missing_minic_c_owners": missing,
              "postlink_target_object_manifest_sha256": sha(manifest),
              "verdict": "POSTLINK_C_OWNER_FAIL"}
    mismatched = [name for name, digest in expected.items()
                  if name in actual and sha(required(out / name)) != digest]
    report["c_owner_hash_mismatches"] = mismatched
    if not unexpected and not missing and not mismatched and len(owners) == len(expected):
        report["verdict"] = "POSTLINK_ALL_C_MINIC_SHA_PASS"
    (output / "postlink-c-owner-census.json").write_text(
        json.dumps(report, sort_keys=True, indent=2) + "\n")
    if report["verdict"] != "POSTLINK_ALL_C_MINIC_SHA_PASS":
        raise Rejected("GNU Kbuild postlink C-owner universe/fallback mismatch: "
                       f"added={unexpected[:8]} missing={missing[:8]} "
                       f"overwritten={mismatched[:8]}")
    return report


def vdso_c_owners(rows: list[dict], target: str) -> list[dict]:
    """Find all C-owned vDSO inputs, including each ISA's vDSO glue object."""
    prefixes = {
        "x86_64": ("arch/x86/entry/vdso/",),
        "aarch64": ("arch/arm64/kernel/vdso/",),
        "loongarch64": ("arch/loongarch/vdso/",),
    }
    glue = {
        "x86_64": set(),
        "aarch64": {"arch/arm64/kernel/vdso.o"},
        "loongarch64": {"arch/loongarch/kernel/vdso.o"},
    }
    return sorted((row for row in rows if row["object"] in glue[target] or
                   row["object"].startswith(prefixes[target])),
                  key=lambda row: row["object"])


def gnu_vdso_assembly_command(work: Path, row: dict, data: dict,
                              diagnostic_input: Path, diagnostic_asm: Path) -> list[str]:
    """Replay GNU -S using the *frozen* original GNU -E input and .o.cmd flags.

    This assembly is additional forensic evidence, not a replacement for the
    independently built original GNU object; the original GNU .o remains the
    ABI/link oracle. Never re-preprocess the C owner or rewrite Kbuild files.
    """
    name = PurePosixPath(row["object"])
    cmd_path = work / "build" / name.parent / ("." + name.name + ".cmd")
    argv, _, _, _ = frozen_kbuild_oracle().gnu_argv(
        required(cmd_path).read_text(), data["gcc"], row["object"],
        diagnostic_input.with_suffix(".unused.i"), data["target"])
    if argv[:3] != [data["gcc"], "-E", "-P"]:
        raise Rejected(f"invalid GNU -E replay for vDSO owner {name}")
    source_indices = [i for i, token in enumerate(argv) if
                      token.endswith(".c") and not token.startswith("-")]
    if len(source_indices) != 1:
        raise Rejected(f"ambiguous vDSO GNU C source: {name}")
    # The frozen .i is a preprocessed C translation unit. -x cpp-output
    # avoids a second preprocessor pass even with original Kbuild -I/-D flags.
    argv = [data["gcc"], "-S", *argv[3:]]
    source_index = source_indices[0] - 1
    argv[source_index:source_index + 1] = ["-x", "cpp-output", str(diagnostic_input), "-x", "none"]
    argv[-1] = str(diagnostic_asm)
    return argv


def preserve_vdso_relocation_differential(work: Path, staged: Path,
                                          output: Path, rows: list[dict],
                                          data: dict) -> None:
    """Save strict GNU-vs-MiniC vDSO ELF and source evidence BEFORE overlay.

    x86 checkundef and arm64's dynamic-relocation gate can delete the failing
    .so.dbg. Save both original/staged C owner objects and target-readelf
    relocations/symbols, target-nm undefined symbols, plus both assembly views.
    The generated GNU .s is a diagnostic replay from frozen GNU .i; the
    original GNU .o is always preserved unchanged and remains authoritative.
    Diagnostic tool/replay failures are written to index.json, not used to
    weaken or alter the original GNU Kbuild linker and runtime acceptance.
    """
    target = data["target"]
    selected = vdso_c_owners(rows, target)
    diag = output / "vdso-diagnostics"
    diag.mkdir(parents=True, exist_ok=False)
    report = {"schema": "minic-three-isa-vdso-elf-ab-v1", "target": target,
              "original_gnu_kbuild_objects_are_authoritative": True,
              "gnu_assembly_origin": "gnu-S-replay-of-frozen-gnu-E-i-with-authentic-o.cmd-flags",
              "diagnostic_only_never_changes_link_or_verdict": True,
              "owners": [], "diagnostic_errors": []}
    tools = {"readelf": data["cross"] + "readelf", "nm": data["cross"] + "nm"}
    # Native x86 uses the host GNU binutils; the other two use target binutils.
    for row in selected:
        name = PurePosixPath(row["object"])
        owner_dir = diag / name.with_suffix("")
        owner_dir.mkdir(parents=True, exist_ok=False)
        entry: dict = {"owner": str(name), "gnu_kbuild_cmd_sha256": row["cmd_sha256"],
                       "frozen_input_sha256": row["sha256"], "variants": {}}
        for variant, obj in (("gnu", required(work / "build" / name)),
                             ("minic", required(staged / name))):
            copied = owner_dir / f"{variant}.o"
            shutil.copyfile(obj, copied)
            item: dict = {"object_sha256": sha(copied), "diagnostics": {}}
            entry["variants"][variant] = item
            for diagnostic, tool, options in (
                ("readelf-rW", tools["readelf"], ["-rW"]),
                ("readelf-sW", tools["readelf"], ["-sW"]),
                ("nm-u", tools["nm"], ["-u"]),
            ):
                destination = owner_dir / f"{variant}.{diagnostic}.txt"
                try:
                    command = subprocess.run([tool, *options, str(copied)],
                                             text=True, capture_output=True, timeout=30)
                    destination.write_text(command.stdout + command.stderr)
                    item["diagnostics"][diagnostic] = {"exit_code": command.returncode}
                    if command.returncode:
                        report["diagnostic_errors"].append(
                            f"{name}:{variant}:{diagnostic}:rc={command.returncode}")
                except (OSError, subprocess.TimeoutExpired) as exc:
                    destination.write_text(f"DIAGNOSTIC_ERROR: {exc}\n")
                    item["diagnostics"][diagnostic] = {"error": str(exc)}
                    report["diagnostic_errors"].append(f"{name}:{variant}:{diagnostic}:{exc}")
        mini_asm = required(staged / name.with_suffix(".s"))
        shutil.copyfile(mini_asm, owner_dir / "minic.s")
        frozen = required(work / "frozen" / row["input"])
        shutil.copyfile(frozen, owner_dir / "original.gnu.i")
        original_cmd = required(work / "build" / name.parent / ("." + name.name + ".cmd"))
        shutil.copyfile(original_cmd, owner_dir / "original.gnu.o.cmd")
        gnu_asm = owner_dir / "gnu.s"
        try:
            command = gnu_vdso_assembly_command(work, row, data, frozen, gnu_asm)
            replay = subprocess.run(command, cwd=work / "build", text=True,
                                    capture_output=True, timeout=60)
            (owner_dir / "gnu-s-replay.stderr.txt").write_text(replay.stderr)
            entry["gnu_assembly_replay_rc"] = replay.returncode
            if replay.returncode or not gnu_asm.is_file() or not gnu_asm.stat().st_size:
                report["diagnostic_errors"].append(f"{name}:gnu-S-replay:rc={replay.returncode}")
        except (OSError, subprocess.TimeoutExpired, Rejected) as exc:
            (owner_dir / "gnu-s-replay.stderr.txt").write_text(f"DIAGNOSTIC_ERROR: {exc}\n")
            entry["gnu_assembly_replay_error"] = str(exc)
            report["diagnostic_errors"].append(f"{name}:gnu-S-replay:{exc}")
        report["owners"].append(entry)
    report["verdict"] = ("VDSO_DIAGNOSTIC_COMPLETE" if selected and
                         not report["diagnostic_errors"] else "VDSO_DIAGNOSTIC_PARTIAL")
    (diag / "index.json").write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print(f"THREE_ISA_VDSO_ELF_AB target={target} verdict={report['verdict']} "
          f"owners={len(selected)} diagnostic_errors={len(report['diagnostic_errors'])}")


def relink(work: Path, output: Path, rows: list[dict], results: list[dict],
           data: dict, gnu_receipt: dict) -> Path:
    out, source = work / "build", work / "linux-6.6.143"
    if (len(rows) != len(results) or any(row["status"] != "MINIC_S_GNU_AS_PASS" for row in results)):
        raise Rejected("a MiniC -S or GNU AS C owner failed; do NOT mix GNU C objects")
    pool = output / "staged"
    expected = {row["object"]: row["obj_sha256"] for row in results}
    if len(expected) != data["gnu_c_owners"]:
        raise Rejected("duplicate candidate ELF64 C owners")
    for name, hash_ in expected.items():
        staged = required(pool / name)
        if sha(staged) != hash_:
            raise Rejected(f"corrupt staged MiniC object: {name}")
        elf_machine(staged, data["machine"])
    # Measure the SAME C owners before overlay destroys the disposable GNU
    # originals. This is read-only evidence; failures cannot alter the GNU
    # linker command, suppress its errors or certify the resulting Image.
    diagnostics = module("minic_link_sections", "linux-three-isa-section-size-v1.py")
    sizes = diagnostics.capture_comparison(
        out, pool, rows, data["machine"], output / "c-section-sizes.json")
    print(f"THREE_ISA_C_SECTION_DIAGNOSTIC target={data['target']} "
          f"verdict={sizes['verdict']} owners={sizes.get('owners_measured', 0)}/{len(rows)}")
    preserve_vdso_relocation_differential(work, pool, output, rows, data)
    # Mutate ONLY the disposable GNU Kbuild worktree after the original
    # GNU/QEMU receipt was produced and preserved.  Kbuild's original .cmd
    # dependency graph must rearchive every changed owner and relink.
    for name in expected:
        shutil.copyfile(pool / name, out / name)
        if sha(out / name) != expected[name]:
            raise Rejected(f"copy failed before Kbuild relink: {name}")
    env = dict(os.environ, KBUILD_BUILD_USER="minic", KBUILD_BUILD_HOST="minic-ci",
               KBUILD_BUILD_TIMESTAMP="Thu Jan  1 00:00:00 UTC 1970")
    command = ["make", "-C", str(source), "O=" + str(out),
               "ARCH=" + data["arch"], "CROSS_COMPILE=" + data["cross"],
               "CC=" + data["gcc"], "-j4", data["build_target"]]
    linked = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, env=env)
    (output / "gnu-ld-relink.log").write_text(linked.stdout)
    layout = diagnostics.capture_link_layout(
        out, output / "link-layout", data["target"], data["machine"])
    print(f"THREE_ISA_LINK_LAYOUT_DIAGNOSTIC target={data['target']} "
          f"verdict={layout['verdict']} map={layout['gnu_ld_map']}")
    if linked.returncode:
        raise Rejected(f"original GNU Kbuild relink failed rc={linked.returncode}")
    # Re-enumerate *all* live original-Kbuild C owners after the link, not just
    # the preoverlay list. A new GCC-generated C object may not evade this
    # union check even if each previous MiniC .o remained byte-for-byte intact.
    census = verify_postlink_c_owners(out, output, data["target"], expected)
    verify_gnu_kbuild_commands(out, rows, data["gcc"])
    (output / "postlink-gnu-c-fallback.json").write_text(json.dumps(
        {"verdict": census["verdict"], "gnu_c_fallback_count": 0,
         "unexpected": [], "overwritten": []}, indent=2) + "\n")
    # Ensure this wasn't a no-op link with an unchanged original GNU image.
    if not re.search(r"\bLD\b.*(?:vmlinux|Image)|\bAR\b.*built-in", linked.stdout):
        raise Rejected("no live GNU Kbuild rearchive/relink evidence")
    new_image = required(out / data["image_name"])
    if sha(new_image) == gnu_receipt["image_sha256"]:
        raise Rejected("kernel Image remained byte-for-byte GNU; overlay/relink is unproven")
    return new_image


def boot_and_compare(data: dict, output: Path, initramfs: Path,
                     firmware: Path | None, image: Path,
                     gnu_receipt: dict, golden_logs: Path) -> dict:
    oracle = module("minic_qemu_oracle", "linux-three-isa-gnu-qemu-v1.py")
    qemu = oracle.executable(oracle.TARGETS[data["target"]]["qemu"])
    if data["target"] == "loongarch64":
        if firmware is None or sha(required(firmware)) != gnu_receipt.get("firmware_sha256"):
            raise Rejected("LoongArch64 requires exact same SHA-pinned GNU QEMU EFI firmware")
    elif firmware is not None:
        raise Rejected("firmware only valid for LoongArch64")
    boot_dir = output / "boot"
    boot_dir.mkdir()
    result = dict(schema="minic-three-isa-qemu-candidate-v1", target=data["target"],
                  source_sha256=data["source_sha256"],
                  config_sha256=data["config_sha256"],
                  initramfs_sha256=data["initramfs_sha256"],
                  firmware_sha256=data["firmware_sha256"],
                  image_sha256=sha(image),
                  compiler_sha256=data["compiler_sha256"],
                  compiler_driver_sha256=data["compiler_driver_sha256"],
                  compiler_cc_sha256=data["compiler_cc_sha256"],
                  golden_c_owners=data["gnu_c_owners"],
                  minic_c_owners=data["gnu_c_owners"], gnu_c_fallback_count=0,
                  lanes=[], verdict="MINIC_QEMU_NOT_CERTIFIED")
    for profile in ("p0", "p1"):
        lane = oracle.run_lane(data["target"], qemu, image, initramfs, firmware,
                               profile, "2G", 240, boot_dir)
        lane["verdict"] = "MINIC_QEMU_RUNTIME_PASS"
        result["lanes"].append(lane)
    result["verdict"] = MINIC_VERDICT
    receipt = boot_dir / "minic-qemu-receipt.json"
    receipt.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    # The two executions have independent Image hashes and QEMU exits; the
    # semantic comparator validates the identical GNU initramfs oracle.
    comparator = Path(__file__).with_name("linux-four-isa-runtime-diff-v1.py")
    for profile in ("p0", "p1"):
        command = [sys.executable, str(comparator),
                   "--golden-log", str(golden_logs / f"{profile}.log"),
                   "--minic-log", str(boot_dir / f"{profile}.log"),
                   "--golden-receipt", str(output / "gnu-qemu-receipt.json"),
                   "--minic-receipt", str(receipt),
                   "--target", data["target"], "--profile", profile,
                   "--receipt", str(boot_dir / f"{profile}-differential.json")]
        subprocess.run(command, check=True)
    return result


def self_test() -> None:
    assert set(TARGETS) == {"x86_64", "aarch64", "loongarch64"}
    assert len(ALLOWED_CFLAGS) == 16
    assert len(LINUX_SHA) == 64
    module("minic_link_sections_test", "linux-three-isa-section-size-v1.py").self_test()
    failures_checked = 0
    abi_row = {"object": "arch/loongarch/vdso/vgetcpu.o",
               "gnu_as_flags": ["-mabi=lp64s", "-mcmodel=normal", "-msoft-float"],
               "gnu_elf_abi": 1}
    command = gnu_assembler_command("loongarch64-linux-gnu-gcc-13", abi_row,
                                     Path("vgetcpu.s"), Path("vgetcpu.o"))
    assert command[1:4] == abi_row["gnu_as_flags"]
    assert command[-6:] == ["-x", "assembler", "-c", "vgetcpu.s", "-o", "vgetcpu.o"]
    for invalid in (["-mabi=lp64s\n-o", "/tmp/foreign"], ["--sysroot=/tmp/foreign"]):
        abi_row["gnu_as_flags"] = invalid
        try:
            gnu_assembler_command("gcc", abi_row, Path("in.s"), Path("out.o"))
        except Rejected:
            failures_checked += 1
        else:
            raise AssertionError("unsafe GNU AS option escaped frozen-Kbuild gate")
    abi_row["gnu_as_flags"] = ["-mabi=lp64s"]
    with __import__("tempfile").TemporaryDirectory() as temp:
        bad = Path(temp) / "not-elf.o"
        bad.write_bytes(b"x" * 64)
        try:
            elf_machine(bad, 62)
        except Rejected:
            pass
        else:
            raise AssertionError("incorrectly accepted foreign object")
        original = Path(temp) / "loong.o"
        elf = bytearray(64)
        elf[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<HHI", elf, 16, 1, 258, 1)
        struct.pack_into("<I", elf, 48, 1)
        original.write_bytes(elf)
        validate_original_abi(original, "loongarch64", abi_row)
        struct.pack_into("<I", elf, 48, 3)
        original.write_bytes(elf)
        try:
            validate_original_abi(original, "loongarch64", abi_row)
        except Rejected:
            failures_checked += 1
        else:
            raise AssertionError("LP64D candidate escaped original LP64S ELF guard")
        # x86 checkundef and arm64's relocation gate delete the failed ELF.
        # Regress the full GNU/MiniC relocation and frozen-.i assembly A/B.
        vdso_owner = "arch/x86/entry/vdso/vgetcpu.o"
        vdso_work = Path(temp) / "vdso-work"
        gnu_obj = vdso_work / "build" / vdso_owner
        mini_obj = Path(temp) / "vdso-mini" / vdso_owner
        for obj, program in ((gnu_obj, "ret"),
                             (mini_obj, "call __minic_vdso_missing_helper\nret")):
            obj.parent.mkdir(parents=True, exist_ok=True)
            source = obj.with_suffix(".s")
            source.write_text(".text\n.globl vdso_probe\nvdso_probe:\n" + program + "\n")
            subprocess.run(["gcc", "-x", "assembler", "-c", str(source),
                            "-o", str(obj)], check=True)
        vdso_cmd = gnu_obj.parent / ".vgetcpu.o.cmd"
        vdso_cmd.write_text(
            f"savedcmd_{vdso_owner} := gcc -O2 -c -o {vdso_owner} "
            "arch/x86/entry/vdso/vgetcpu.c\n")
        vdso_i = vdso_work / "frozen/i/arch/x86/entry/vdso/vgetcpu.i"
        vdso_i.parent.mkdir(parents=True)
        vdso_i.write_text("int original_vdso_probe(void) { return 7; }\n")
        vdso_row = {"object": vdso_owner, "cmd_sha256": sha(vdso_cmd),
                    "input": "i/arch/x86/entry/vdso/vgetcpu.i", "sha256": sha(vdso_i)}
        vdso_output = Path(temp) / "vdso-candidate"
        vdso_output.mkdir()
        preserve_vdso_relocation_differential(
            vdso_work, Path(temp) / "vdso-mini", vdso_output,
            [vdso_row], {"target": "x86_64", "cross": "", "gcc": "gcc"})
        diag = vdso_output / "vdso-diagnostics/arch/x86/entry/vdso/vgetcpu"
        assert "__minic_vdso_missing_helper" in (
            diag / "minic.nm-u.txt").read_text()
        assert "__minic_vdso_missing_helper" not in (
            diag / "gnu.nm-u.txt").read_text()
        assert "__minic_vdso_missing_helper" in (
            diag / "minic.readelf-rW.txt").read_text()
        assert "__minic_vdso_missing_helper" not in (
            diag / "gnu.readelf-rW.txt").read_text()
        for variant in ("gnu", "minic"):
            assert (diag / f"{variant}.readelf-rW.txt").is_file()
            assert (diag / f"{variant}.readelf-sW.txt").is_file()
            assert (diag / f"{variant}.o").is_file()
            assert (diag / f"{variant}.s").is_file()
        assert "original_vdso_probe" in (diag / "gnu.s").read_text()
        assert json.loads((vdso_output / "vdso-diagnostics/index.json").read_text())[
            "verdict"] == "VDSO_DIAGNOSTIC_COMPLETE"
        assert [row["object"] for row in vdso_c_owners(
            [{"object": "arch/arm64/kernel/vdso.o"},
             {"object": "arch/arm64/kernel/vdso/vgettimeofday.o"},
             {"object": "arch/arm64/kernel/init.o"}], "aarch64")] == [
                 "arch/arm64/kernel/vdso.o", "arch/arm64/kernel/vdso/vgettimeofday.o"]
        assert len(vdso_c_owners([
            {"object": "arch/loongarch/kernel/vdso.o"},
            {"object": "arch/loongarch/vdso/vgetcpu.o"},
            {"object": "arch/loongarch/vdso/vgettimeofday.o"}], "loongarch64")) == 3
        failures_checked += 1
        # Preserve only the first eight authentic frozen failures; no GCC
        # replay or fallback is permitted even when ten owners failed.
        first_work = Path(temp) / "first-errors-work"
        first_out = Path(temp) / "first-errors-output"
        first_out.mkdir()
        failed_rows = []
        failed_results = []
        for i in range(10):
            name = f"kernel/first_error_{i}.o"
            original = first_work / "build" / name
            original.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(gnu_obj, original)
            cmd = original.parent / ("." + original.name + ".cmd")
            cmd.write_text(f"savedcmd_{name} := gcc -c -o {name} {name[:-2]}.c\n")
            frozen_path = first_work / "frozen/i" / PurePosixPath(name).with_suffix(".i")
            frozen_path.parent.mkdir(parents=True, exist_ok=True)
            frozen_path.write_text(f"/* untouched GNU -E owner {i} */\n")
            staged_error = first_out / "staged" / PurePosixPath(name).with_suffix(".tool-stderr.txt")
            staged_error.parent.mkdir(parents=True, exist_ok=True)
            staged_error.write_text(f"COMPLETE_MINIC_STDERR_FOR_OWNER_{i}\n" + "trace\n" * 512)
            if i == 1:
                staged_error.with_name(f"first_error_{i}.s").write_text(".text\n")
            failed_rows.append({"object": name, "cmd_sha256": sha(cmd),
                                "input": "i/" + str(PurePosixPath(name).with_suffix(".i")),
                                "sha256": sha(frozen_path)})
            failed_results.append({"object": name, "status": "MINIC_S_FAIL",
                                   "detail": "bounded 600-byte error tail"})
        first_diagnostics = preserve_first_failed_owners(
            first_work, first_out, failed_rows, failed_results)
        assert first_diagnostics["verdict"] == "FIRST_FAILED_OWNER_DIAGNOSTICS_COMPLETE"
        assert first_diagnostics["total_failed_owners"] == 10
        assert first_diagnostics["sample_count"] == 8
        first_owner = first_out / "failed-owner-diagnostics/kernel/first_error_0"
        assert "COMPLETE_MINIC_STDERR_FOR_OWNER_0" in (
            first_owner / "tool-stderr.txt").read_text()
        assert (first_owner / "original.gnu.o").read_bytes() == gnu_obj.read_bytes()
        assert sha(first_owner / "original.gnu.i") == failed_rows[0]["sha256"]
        assert sha(first_owner / "original.gnu.o.cmd") == failed_rows[0]["cmd_sha256"]
        assert (first_out / "failed-owner-diagnostics/kernel/first_error_1/minic.s").is_file()
        assert not (first_out / "failed-owner-diagnostics/kernel/first_error_8").exists()
        assert len((first_owner / "tool-stderr.txt").read_text()) > 600
        failures_checked += 1
        # Linux kernel/configs.o has a genuine Kbuild-relative .incbin path.
        # Wrong cwd is an AS failure even when MiniC emitted valid assembly.
        kbuild = Path(temp) / "kernel-build"
        (kbuild / "kernel").mkdir(parents=True)
        (kbuild / "kernel" / "config_data.gz").write_bytes(b"frozen-kconfig")
        embedded = Path(temp) / "kernel-configs.s"
        embedded.write_text('.section .rodata\n.incbin "kernel/config_data.gz"\n')
        embedded_obj = Path(temp) / "kernel-configs.o"
        embedded_row = {"object": "kernel/configs.o", "gnu_as_flags": []}
        subprocess.run(gnu_assembler_command("gcc", embedded_row,
                                             embedded, embedded_obj),
                       cwd=kbuild, capture_output=True, check=True)
        assert embedded_obj.stat().st_size > 64
        failures_checked += 1
        out = Path(temp) / "out"
        evidence = Path(temp) / "evidence"
        out.mkdir()
        evidence.mkdir()

        def sample_c_owner(name: str) -> Path:
            path = out / name
            path.parent.mkdir(parents=True, exist_ok=True)
            elf = bytearray(64)
            elf[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<HH", elf, 16, 1, 62)  # ELF64 ET_REL EM_X86_64
            path.write_bytes(elf)
            cmd = path.parent / ("." + path.name + ".cmd")
            cmd.write_text(f"savedcmd_{name} := gcc -c -o {name} {name[:-2]}.c\n"
                           f"source_{name} := {name[:-2]}.c\n")
            return path

        first = sample_c_owner("kernel/canonical.o")
        expected = {"kernel/canonical.o": sha(first)}
        cmd = first.parent / ("." + first.name + ".cmd")
        metadata = [{"object": "kernel/canonical.o", "cmd_sha256": sha(cmd),
                     "gnu_as_flags": []}]
        verify_gnu_kbuild_commands(out, metadata, "gcc")
        metadata[0]["gnu_as_flags"] = ["-mabi=lp64s"]
        try:
            verify_gnu_kbuild_commands(out, metadata, "gcc")
        except Rejected:
            failures_checked += 1
        else:
            raise AssertionError("forged original GNU AS flags escaped .cmd gate")
        metadata[0]["gnu_as_flags"] = []
        assert verify_postlink_c_owners(out, evidence, "x86_64", expected)["verdict"] == \
            "POSTLINK_ALL_C_MINIC_SHA_PASS"
        added = sample_c_owner("kernel/gnu-escaped.o")
        try:
            verify_postlink_c_owners(out, evidence, "x86_64", expected)
        except Rejected as exc:
            assert "added=['kernel/gnu-escaped.o']" in str(exc)
            failures_checked += 1
        else:
            raise AssertionError("new postlink GNU C object escaped the owner gate")
        added.unlink()
        first.write_bytes(first.read_bytes() + b"GNU_TAMPER")
        try:
            verify_postlink_c_owners(out, evidence, "x86_64", expected)
        except Rejected as exc:
            assert "overwritten=['kernel/canonical.o']" in str(exc)
            failures_checked += 1
        else:
            raise AssertionError("GNU rewrite of a MiniC .o escaped the SHA gate")
        first.unlink()
        try:
            verify_postlink_c_owners(out, evidence, "x86_64", expected)
        except Rejected as exc:
            assert "missing=['kernel/canonical.o']" in str(exc)
            failures_checked += 1
        else:
            raise AssertionError("missing original C owner escaped postlink gate")
        cmd.write_text(cmd.read_text() + "# tampered after GNU freeze\n")
        try:
            verify_gnu_kbuild_commands(out, metadata, "gcc")
        except Rejected:
            failures_checked += 1
        else:
            raise AssertionError("changed GNU .o.cmd escaped the immutable source/flags gate")

        bindir = Path(temp) / "bin"
        bindir.mkdir()
        driver, core = bindir / "minic", bindir / "minic-cc"
        for path in (driver, core):
            path.write_text("#!/bin/sh\nexit 0\n")
            path.chmod(0o755)
        identity = compiler_pair(driver)
        assert identity["compiler_sha256"] == identity["compiler_driver_sha256"]
        core.write_text("#!/bin/sh\nexit 1\n")
        try:
            compiler_pair(driver, identity)
        except Rejected:
            failures_checked += 1
        else:
            raise AssertionError("modified minic-cc escaped binary identity gate")
        core.write_text("#!/bin/sh\nexit 0\n")
        driver.write_text("#!/bin/sh\nexit 2\n")
        try:
            compiler_pair(driver, identity)
        except Rejected:
            failures_checked += 1
        else:
            raise AssertionError("modified MiniC driver escaped binary identity gate")
    print("THREE_ISA_ALL_MINIC_CANDIDATE_SELFTEST=PASS targets=3 "
          f"postlink_and_binary_negative_cases={failures_checked}")


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--target", choices=tuple(TARGETS))
    p.add_argument("--gnu-work", type=Path)
    p.add_argument("--minic", type=Path)
    p.add_argument("--gnu-receipt", type=Path)
    p.add_argument("--initramfs", type=Path)
    p.add_argument("--gnu-boot-logs", type=Path)
    p.add_argument("--firmware", type=Path)
    p.add_argument("--output", type=Path)
    p.add_argument("--workers", type=int, default=7)
    p.add_argument("--timeout", type=int, default=180)
    a = p.parse_args()
    if a.self_test:
        self_test()
        return 0
    if not all((a.target, a.gnu_work, a.minic, a.gnu_receipt, a.initramfs,
                a.gnu_boot_logs, a.output)):
        p.error("--target --gnu-work --minic --gnu-receipt --initramfs --gnu-boot-logs --output required")
    if not 1 <= a.workers <= 14 or not 30 <= a.timeout <= 900:
        p.error("workers must be 1..14 and timeout 30..900 seconds")
    if a.output.exists():
        p.error("refuse to reuse an old output directory or accept stale runtime evidence")
    a.output.mkdir(parents=True)
    outcome = dict(schema="minic-three-isa-candidate-build-v1", target=a.target,
                   verdict="MINIC_CANDIDATE_NOT_CERTIFIED")
    try:
        golden = json.loads(required(a.gnu_receipt).read_text())
        rows, data = candidate_inputs(a.target, a.gnu_work, a.minic, golden, a.initramfs)
        outcome.update(data)
        shutil.copyfile(a.gnu_receipt, a.output / "gnu-qemu-receipt.json")
        outcome["owner_count"] = len(rows)
        results = make_objects(rows, a.gnu_work, a.minic, a.output, data, a.workers, a.timeout)
        compiler_pair(a.minic, data)
        failed = [row for row in results if row["status"] != "MINIC_S_GNU_AS_PASS"]
        (a.output / "all-c-owners-result.json").write_text(json.dumps(results, indent=2) + "\n")
        outcome.update(minic_s_gnu_as_pass=len(results) - len(failed), minic_s_gnu_as_fail=len(failed))
        if failed:
            preserve_first_failed_owners(a.gnu_work, a.output, rows, results)
            raise Rejected(f"incomplete MiniC/GNU AS C owner coverage {len(results)-len(failed)}/{len(rows)}; first={failed[0]}")
        image = relink(a.gnu_work, a.output, rows, results, data, golden)
        compiler_pair(a.minic, data)
        outcome["candidate_image_sha256"] = sha(image)
        # Never promote QEMU/same-source results if object provenance failed.
        boot_and_compare(data, a.output, a.initramfs, a.firmware,
                         image, golden, a.gnu_boot_logs)
        outcome["verdict"] = "MINIC_ALL_C_GNU_LINK_QEMU_STRICT_DIFF_PASS"
        print(f"THREE_ISA_MINIC_FULL_RUNTIME=PASS target={a.target} "
              f"owners={len(rows)}/{len(rows)} fallback_GNU_C=0 QEMU=P0_P1 GNU_diff=PASS")
        rc = 0
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        outcome["verdict"] = "MINIC_ALL_C_GNU_LINK_QEMU_FAIL"
        outcome["error"] = str(exc)
        print(f"THREE_ISA_MINIC_FULL_RUNTIME=FAIL target={a.target} reason={exc}", file=sys.stderr)
        rc = 2
    (a.output / "candidate-receipt.json").write_text(json.dumps(outcome, sort_keys=True, indent=2) + "\n")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
