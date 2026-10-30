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
from collections import Counter
import concurrent.futures
import contextlib
import functools
import hashlib
import importlib.util
import io
import json
import mmap
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
MINIC_PRE_DIFF_VERDICT = "MINIC_QEMU_P0_P1_BOOTED_DIFF_PENDING"


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


def verify_gnu_kbuild_commands(out: Path, rows: list[dict], gcc: str,
                               target: str | None = None) -> dict:
    """Verify canonical GNU flags and report unqualified codegen options.

    This remains diagnostic during the full C/AS/link/QEMU campaign: missing
    feature equivalence must be visible, never silently promoted to strict
    source-option qualification or used to skip the next Linux error.
    """
    missing = Counter()
    unqualified_owners = 0
    for row in rows:
        name = PurePosixPath(row["object"])
        cmd = out / name.parent / ("." + name.name + ".cmd")
        digest = row.get("cmd_sha256")
        if (not isinstance(digest, str) or
                not re.fullmatch(r"[0-9a-f]{64}", digest) or
                sha(required(cmd)) != digest):
            raise Rejected(f"original GNU Kbuild .cmd provenance changed: {name}")
        try:
            raw_cmd = cmd.read_text()
            _, authentic_minic_flags, authentic_as_flags, _ = frozen_kbuild_oracle().gnu_argv(
                raw_cmd, gcc, name.as_posix(), Path("unused.i"), target)
        except (OSError, ValueError) as exc:
            raise Rejected(f"invalid authentic GNU machine options for {name}: {exc}") from exc
        if row.get("gnu_as_flags") != authentic_as_flags:
            raise Rejected(f"original GNU AS machine options changed: {name}")
        if row.get("minic_flags") != authentic_minic_flags:
            raise Rejected(f"original GNU C language/ABI options changed: {name}")
        options = frozen_kbuild_oracle().unqualified_gnu_codegen_flags(
            frozen_kbuild_oracle().gnu_command_tokens(raw_cmd, gcc),
            authentic_minic_flags)
        if options:
            unqualified_owners += 1
            missing.update(options)
    status = ("INCONCLUSIVE_UNQUALIFIED" if missing else
              "NO_UNQUALIFIED_GNU_CODEGEN_OPTIONS")
    return {"gnu_c_option_equivalence": status,
            "gnu_c_option_unqualified_owners": unqualified_owners,
            "gnu_c_option_unqualified_counts": dict(sorted(missing.items()))}


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
    option_contract = verify_gnu_kbuild_commands(out, rows, gcc, target)
    print(f"GNU_C_OPTION_EQUIVALENCE={option_contract['gnu_c_option_equivalence']} "
          f"target={target} owners={option_contract['gnu_c_option_unqualified_owners']}/{len(rows)} "
          f"distinct_options={len(option_contract['gnu_c_option_unqualified_counts'])}")
    for row in rows:
        name = PurePosixPath(row["object"])
        allowed = ALLOWED_CFLAGS | (LOONGARCH_ABI_FLAGS if target == "loongarch64" else set()) | (
            frozen_kbuild_oracle().X86_CODEGEN_FLAGS if target == "x86_64" else set())
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
                  **tool_identity, **option_contract, "arch": arch, "cross": cross,
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


def preserve_link_failed_c_owners(work: Path, output: Path, rows: list[dict],
                                  results: list[dict], link_log: Path,
                                  limit: int = 32,
                                  byte_budget: int = 96 * 1024 * 1024) -> dict:
    """Preserve a bounded, SHA-verified subset of *original* GNU frozen C.

    GNU LD may identify an owner only as ``util.o`` even when the authentic
    manifest has many directories containing that basename. Never guess one:
    record the ambiguity, and capture only full-path or unique-basename owner
    identities. This is post-failure evidence, never a GNU-C compilation
    fallback, a changed ELF, or a different Kbuild/link invocation.
    """
    if not 1 <= limit <= 64 or not 1 <= byte_budget <= 256 * 1024 * 1024:
        raise Rejected("invalid bounded link-failure diagnostic budget")
    if len(rows) != len(results) or any(
            item.get("status") != "MINIC_S_GNU_AS_PASS" for item in results):
        raise Rejected("link-failure samples require a complete MiniC C-object union")
    log_text = required(link_log).read_text(errors="replace")
    by_name = {row["object"]: row for row in rows}
    by_result = {item["object"]: item for item in results}
    if len(by_name) != len(rows) or set(by_name) != set(by_result):
        raise Rejected("link-failure source and MiniC object inventory differ")
    # GNU LD often prints an archive member's basename, but a *unique C*
    # basename does not prove origin: a different original GNU .S owner can
    # have the same basename. Consult the complete original target-ELF64
    # inventory, including non-C objects, before accepting any basename.
    discovery = json.loads(required(work / "evidence/discovery.json").read_text())
    target_inventory = required(work / "evidence/gnu-target-objects.sha256")
    all_target_names: set[str] = set()
    for line in target_inventory.read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (\S+)", line)
        if match is None:
            raise Rejected("malformed original GNU target-ELF64 object inventory")
        name = match.group(2)
        path = PurePosixPath(name)
        if path.is_absolute() or ".." in path.parts or path.suffix != ".o":
            raise Rejected("unsafe original GNU target ELF64 owner path")
        if name in all_target_names:
            raise Rejected("duplicate original GNU target ELF64 owner")
        all_target_names.add(name)
    if (len(all_target_names) != discovery.get("gnu_target_objects") or
            not set(by_name).issubset(all_target_names)):
        raise Rejected("incomplete original GNU target ELF64/ASM provenance")
    by_basename: dict[str, list[str]] = {}
    for name in all_target_names:
        path = PurePosixPath(name)
        by_basename.setdefault(path.name, []).append(name)
    for owners in by_basename.values():
        owners.sort()
    owner_pattern = re.compile(r"(?<![A-Za-z0-9_./+-])"
                               r"([A-Za-z0-9_.+-]+(?:/[A-Za-z0-9_.+-]+)*\.o):(?:\(|:)")
    referenced: list[str] = []
    ambiguous: dict[str, list[str]] = {}
    unknown: set[str] = set()
    all_mentions = 0
    for line in log_text.splitlines():
        if "undefined reference to" not in line and "relocation truncated to fit" not in line:
            continue
        for match in owner_pattern.finditer(line):
            all_mentions += 1
            token = match.group(1)
            if "/" in token and token in by_name:
                chosen = token
            else:
                candidates = by_basename.get(PurePosixPath(token).name, [])
                if len(candidates) == 1 and candidates[0] in by_name:
                    chosen = candidates[0]
                elif candidates:
                    ambiguous[token] = candidates[:16]
                    continue
                else:
                    unknown.add(token)
                    continue
            if chosen not in referenced:
                referenced.append(chosen)

    dest = output / "link-failure-owner-diagnostics"
    dest.mkdir(parents=True, exist_ok=False)
    report = {"schema": "minic-three-isa-link-failure-c-owners-v1",
              "diagnostics_only": True, "no_gcc_retry_or_fallback": True,
              "original_link_log_sha256": sha(link_log),
              "total_link_owner_mentions": all_mentions,
              "unambiguously_resolved_owners": len(referenced),
              "sample_limit": limit, "byte_budget": byte_budget,
              "bytes_copied": 0, "samples": [], "skipped": [],
              "ambiguous_basenames": [
                  {"token": name, "candidate_count": len(by_basename.get(
                      PurePosixPath(name).name, [])), "sample_candidates": owners}
                  for name, owners in sorted(ambiguous.items())[:64]],
              "unknown_non_c_owners": sorted(unknown)[:64], "errors": []}
    for name in referenced:
        if len(report["samples"]) >= limit:
            report["skipped"].append({"owner": name, "reason": "owner_count_budget"})
            continue
        row = by_name[name]
        source_name = PurePosixPath(row.get("input", ""))
        if (source_name.is_absolute() or ".." in source_name.parts or
                source_name.suffix != ".i"):
            raise Rejected("unsafe original frozen GNU input in link diagnostics")
        owner_path = PurePosixPath(name)
        original_i = required(work / "frozen" / source_name)
        original_cmd = required(work / "build" / owner_path.parent /
                                ("." + owner_path.name + ".cmd"))
        if (sha(original_i) != row.get("sha256") or
                sha(original_cmd) != row.get("cmd_sha256")):
            raise Rejected("frozen GNU C owner changed before link diagnostics")
        # Copy complete files only. A large source can exceed the *remaining*
        # budget; record its identity/size, never create truncated fake .i.
        essential = (("original.gnu.i", original_i, row["sha256"]),
                     ("original.gnu.o.cmd", original_cmd, row["cmd_sha256"]))
        size = sum(path.stat().st_size for _, path, _ in essential)
        if size > byte_budget - report["bytes_copied"]:
            report["skipped"].append({"owner": name, "reason": "byte_budget",
                                      "required_bytes": size})
            continue
        folder = dest / owner_path.with_suffix("")
        folder.mkdir(parents=True, exist_ok=False)
        entry = {"owner": name, "input_sha256": row["sha256"],
                 "cmd_sha256": row["cmd_sha256"], "files": {}}
        for label, path, expected in essential:
            copy = folder / label
            shutil.copyfile(path, copy)
            if sha(copy) != expected:
                raise Rejected("link-failure diagnostic copy changed authentic GNU input")
            entry["files"][label] = {"sha256": expected, "bytes": copy.stat().st_size}
        report["bytes_copied"] += size
        # Candidate S is already MiniC-created and optional; its 1 MiB cap
        # and the global byte cap bound artifact growth. Frozen inputs take
        # priority within each selected owner.
        asm = output / "staged" / owner_path.with_suffix(".s")
        if (asm.is_file() and not asm.is_symlink() and
                asm.stat().st_size <= 1024 * 1024 and
                asm.stat().st_size <= byte_budget - report["bytes_copied"] and
                sha(asm) == by_result[name].get("asm_sha256")):
            copy = folder / "minic.s"
            shutil.copyfile(asm, copy)
            digest = sha(copy)
            if digest != by_result[name]["asm_sha256"]:
                raise Rejected("candidate S changed before link diagnostics")
            entry["files"]["minic.s"] = {"sha256": digest,
                                          "bytes": copy.stat().st_size}
            report["bytes_copied"] += copy.stat().st_size
        report["samples"].append(entry)
    report["sample_count"] = len(report["samples"])
    report["verdict"] = ("LINK_FAILURE_C_OWNER_SAMPLES_COMPLETE" if
                         not report["skipped"] and not report["ambiguous_basenames"] else
                         "LINK_FAILURE_C_OWNER_SAMPLES_BOUNDED")
    (dest / "index.json").write_text(json.dumps(report, sort_keys=True, indent=2) + "\n")
    print("THREE_ISA_LINK_FAILED_OWNER_DIAGNOSTIC "
          f"verdict={report['verdict']} samples={report['sample_count']} "
          f"unambiguous={len(referenced)} bytes={report['bytes_copied']}")
    return report


def summarize_gnu_link_failures(log_text: str) -> dict:
    """Index authentic GNU LD failures without guessing their source owner.

    The complete linker stream remains the authority.  This bounded index
    makes a new exact-head campaign comparable before its artifacts are
    downloaded; it neither changes Kbuild nor suppresses any diagnostic.
    """
    undefined: Counter[str] = Counter()
    relocations: Counter[str] = Counter()
    for line in log_text.splitlines():
        for symbol in re.findall(
                r"undefined reference to [`'‘’]?([^\s`'‘’]+)", line):
            undefined[symbol] += 1
        if "relocation truncated to fit" in line:
            match = re.search(r"relocation truncated to fit:\s*(\S+)", line)
            relocations[match.group(1) if match else "unknown"] += 1
    categories = Counter()
    for symbol, count in undefined.items():
        if symbol.startswith("__compiletime_assert_"):
            category = "compiletime_assert"
        elif symbol.startswith(("__field_overflow", "__bad_copy_")):
            category = "object_bounds"
        elif symbol.startswith("kallsyms_"):
            category = "kallsyms"
        elif "BTF" in symbol:
            category = "btf"
        elif symbol.startswith("__cfi_"):
            category = "cfi"
        else:
            category = "other"
        categories[category] += count
    return {"schema": "minic-three-isa-gnu-link-failures-v1",
            "diagnostics_only": True,
            "undefined_references": sum(undefined.values()),
            "undefined_symbols": len(undefined),
            "categories": dict(sorted(categories.items())),
            "symbols": [{"name": name, "references": count}
                        for name, count in sorted(undefined.items())],
            "truncated_relocations": dict(sorted(relocations.items()))}


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


def positional_c_sources(argv: list[str]) -> list[int]:
    """Exclude forced C includes from GCC's single source argument search."""
    return [i for i, token in enumerate(argv) if
            token.endswith(".c") and not token.startswith("-") and
            (i == 0 or argv[i - 1] not in ("-include", "-imacros"))]


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
    # arm64 vDSO forces `-include lib/vdso/gettimeofday.c` and also compiles
    # arch/arm64/kernel/vdso/vgettimeofday.c as the one positional TU. The
    # included .c is already in frozen GNU -E output, never a second source.
    source_indices = positional_c_sources(argv)
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


def scan_aarch64_relocations(path: Path) -> dict:
    """Read only ELF64 relocation metadata; do not execute/modify the owner.

    GOT relocations are direct evidence of an object's linker GOT demand.
    Undefined CALL26/JUMP26 are *candidates*, not proof of generated PLT:
    a static link might resolve them without a PLT. Bounded samples preserve
    source owners while counts retain the distinction.
    """
    totals = {"got": 0, "plt": 0, "undefined_branch": 0,
              "compiletime_assert_branch": 0,
              "relocations_scanned": 0, "truncated": False,
              "got_samples": [], "plt_samples": [],
              "compiletime_assert_samples": [],
              "undefined_branch_samples": []}
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0,
                                                access=mmap.ACCESS_READ) as image:
        if (len(image) < 64 or image[:6] != b"\x7fELF\x02\x01" or
                struct.unpack_from("<HH", image, 16) != (1, 183)):
            raise ValueError("not relocatable AArch64 little-endian ELF64")
        section_start = struct.unpack_from("<Q", image, 40)[0]
        section_stride, section_count = struct.unpack_from("<HH", image, 58)
        if (section_stride != 64 or section_count < 2 or section_count > 65535 or
                section_start + section_stride * section_count > len(image)):
            raise ValueError("unsafe ELF64 section table")
        sections = [struct.unpack_from("<IIQQQQIIQQ", image,
                                      section_start + 64 * index)
                    for index in range(section_count)]
        for section in sections:
            offset, size = section[4], section[5]
            if section[1] != 8 and (offset > len(image) or
                                    size > len(image) - offset):
                raise ValueError("unsafe ELF64 section bounds")

        def symbol_name(symbol_table_index: int, symbol_index: int) -> tuple[str, bool]:
            if symbol_table_index >= len(sections):
                raise ValueError("invalid relocation symbol table")
            symtab = sections[symbol_table_index]
            sym_offset, sym_size, entry_size, strings_index = (
                symtab[4], symtab[5], symtab[9], symtab[6])
            if symtab[1] not in (2, 11) or entry_size != 24 or strings_index >= len(sections):
                raise ValueError("invalid ELF64 symbol table")
            if symbol_index >= sym_size // entry_size:
                raise ValueError("invalid relocation symbol index")
            sym = sym_offset + symbol_index * entry_size
            name_offset, section_index = struct.unpack_from("<IxxH", image, sym)
            strings = sections[strings_index]
            string_offset, string_size = strings[4], strings[5]
            if name_offset >= string_size:
                return "", section_index == 0
            start = string_offset + name_offset
            end = image.find(b"\0", start, string_offset + string_size)
            if end == -1:
                raise ValueError("unterminated ELF64 symbol name")
            return image[start:end].decode("utf-8", "replace")[:160], section_index == 0

        for section_index, section in enumerate(sections):
            section_type, offset, size, symbol_table_index, stride = (
                section[1], section[4], section[5], section[6], section[9])
            if section_type not in (4, 9):
                continue
            expected_stride = 24 if section_type == 4 else 16
            if stride != expected_stride or size % stride:
                raise ValueError("unsafe ELF64 relocation entry size")
            count = size // stride
            # A diagnostics budget must never make the original Linux link
            # unbounded; report partial coverage explicitly if exceeded.
            budget = max(0, 4000000 - totals["relocations_scanned"])
            if count > budget:
                count = budget
                totals["truncated"] = True
            for index in range(count):
                entry = offset + index * stride
                relocation_offset, info = struct.unpack_from("<QQ", image, entry)
                kind = info & 0xffffffff
                is_got = ((300 <= kind <= 313) or (539 <= kind <= 543) or
                          (560 <= kind <= 569) or kind == 1025)
                is_plt = kind == 1026
                is_branch = kind in (282, 283)
                if not is_got and not is_plt and not is_branch:
                    continue
                name, undefined = symbol_name(symbol_table_index, info >> 32)
                sample = {"symbol": name, "relocation_type": kind,
                          "section_index": section_index, "offset": relocation_offset}
                if is_got:
                    totals["got"] += 1
                    if len(totals["got_samples"]) < 8:
                        totals["got_samples"].append(sample)
                elif is_plt:
                    totals["plt"] += 1
                    if len(totals["plt_samples"]) < 8:
                        totals["plt_samples"].append(sample)
                elif undefined:
                    totals["undefined_branch"] += 1
                    if name.startswith("__compiletime_assert_"):
                        totals["compiletime_assert_branch"] += 1
                        if len(totals["compiletime_assert_samples"]) < 8:
                            totals["compiletime_assert_samples"].append(sample)
                    if len(totals["undefined_branch_samples"]) < 8:
                        totals["undefined_branch_samples"].append(sample)
            totals["relocations_scanned"] += count
            if totals["truncated"]:
                break
    return totals


def capture_aarch64_got_plt_owners(out: Path, staged: Path, output: Path,
                                  rows: list[dict], postlink: bool) -> None:
    """Bounded, diagnostic-only original GNU/MiniC owner relocation A/B.

    Never change an ELF owner, original GNU Kbuild command, or linker verdict.
    A surviving post-link vmlinux.o also includes original GNU assembly owners;
    its undefined branch count must not be mistaken for a definitive PLT count.
    """
    folder = output / "aarch64-got-plt-diagnostic"
    folder.mkdir(parents=True, exist_ok=True)
    report: dict = {"schema": "minic-aarch64-got-plt-diagnostic-v1",
                    "diagnostic_only_never_changes_link_or_verdict": True,
                    "stage": "postlink" if postlink else "preoverlay",
                    "variants": {}, "errors": []}
    variants = (("postlink-vmlinux-o", out / "vmlinux.o", ["vmlinux.o"]),) if postlink else (
        ("original-gnu", out, [row["object"] for row in rows]),
        ("minic-candidate", staged, [row["object"] for row in rows]))
    for label, root, names in variants:
        aggregate: dict = {"owners_requested": len(names), "owners_scanned": 0,
                           "owners_with_got": 0, "got_relocations": 0,
                           "owners_with_plt": 0, "plt_relocations": 0,
                           "undefined_branch_relocations": 0,
                           "compiletime_assert_branch_relocations": 0,
                           "relocation_records_scanned": 0,
                           "truncated_owners": 0, "got_owner_samples": [],
                           "plt_owner_samples": [],
                           "compiletime_assert_owner_samples": [],
                           "undefined_branch_owner_samples": []}
        for name in names[:8192]:
            path = root if postlink else root / name
            try:
                result = scan_aarch64_relocations(required(path))
            except (OSError, ValueError, struct.error) as exc:
                if len(report["errors"]) < 32:
                    report["errors"].append(f"{label}:{name}:{exc}")
                continue
            aggregate["owners_scanned"] += 1
            aggregate["got_relocations"] += result["got"]
            aggregate["plt_relocations"] += result["plt"]
            aggregate["undefined_branch_relocations"] += result["undefined_branch"]
            aggregate["compiletime_assert_branch_relocations"] += (
                result["compiletime_assert_branch"])
            aggregate["relocation_records_scanned"] += result["relocations_scanned"]
            aggregate["truncated_owners"] += int(result["truncated"])
            if result["got"]:
                aggregate["owners_with_got"] += 1
                if len(aggregate["got_owner_samples"]) < 48:
                    aggregate["got_owner_samples"].append(
                        {"object": name, "count": result["got"],
                         "samples": result["got_samples"]})
            if result["plt"]:
                aggregate["owners_with_plt"] += 1
                if len(aggregate["plt_owner_samples"]) < 48:
                    aggregate["plt_owner_samples"].append(
                        {"object": name, "count": result["plt"],
                         "samples": result["plt_samples"]})
            if result["undefined_branch"] and len(
                    aggregate["undefined_branch_owner_samples"]) < 48:
                aggregate["undefined_branch_owner_samples"].append(
                    {"object": name, "count": result["undefined_branch"],
                     "compiletime_assert_count": result["compiletime_assert_branch"],
                     "samples": result["undefined_branch_samples"]})
            if result["compiletime_assert_branch"] and len(
                    aggregate["compiletime_assert_owner_samples"]) < 48:
                aggregate["compiletime_assert_owner_samples"].append(
                    {"object": name,
                     "compiletime_assert_count": result["compiletime_assert_branch"],
                     "samples": result["compiletime_assert_samples"]})
        if len(names) > 8192:
            report["errors"].append(f"{label}:owner count exceeds 8192 diagnostic budget")
        report["variants"][label] = aggregate
    report["verdict"] = ("RELOCATION_DIAGNOSTIC_COMPLETE" if not report["errors"] and
                         all(item["owners_requested"] == item["owners_scanned"] and
                             not item["truncated_owners"]
                             for item in report["variants"].values())
                         else "RELOCATION_DIAGNOSTIC_PARTIAL")
    (folder / ("postlink.json" if postlink else "preoverlay.json")).write_text(
        json.dumps(report, sort_keys=True, indent=2) + "\n")
    print(f"THREE_ISA_AARCH64_GOT_PLT_DIAGNOSTIC stage={report['stage']} "
          f"verdict={report['verdict']} variants={len(report['variants'])} "
          f"errors={len(report['errors'])}")


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
    if data["target"] == "aarch64":
        try:
            capture_aarch64_got_plt_owners(out, pool, output, rows, False)
        except Exception as exc:
            (output / "aarch64-got-plt-preoverlay-error.txt").write_text(
                str(exc)[:4096])
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
    if data["target"] == "aarch64":
        try:
            capture_aarch64_got_plt_owners(out, pool, output, rows, True)
        except Exception as exc:
            (output / "aarch64-got-plt-postlink-error.txt").write_text(
                str(exc)[:4096])
    if linked.returncode:
        # The raw GNU LD stream can be tens of MiB.  Emit a small, exact-head
        # count in the CI console while retaining the complete artifact.
        summary = summarize_gnu_link_failures(linked.stdout)
        (output / "gnu-link-failure-summary.json").write_text(
            json.dumps(summary, sort_keys=True, indent=2) + "\n")
        print(f"THREE_ISA_GNU_LINK_FAILURE_SUMMARY target={data['target']} "
              f"references={summary['undefined_references']} "
              f"symbols={summary['undefined_symbols']} "
              f"categories={json.dumps(summary['categories'], sort_keys=True)} "
              f"relocations={json.dumps(summary['truncated_relocations'], sort_keys=True)}")
        # Only after the original unchanged GNU Kbuild command has failed,
        # retain a bounded set of its *authentic frozen GNU C* error owners.
        # This is postmortem evidence, not a linker retry or a GNU C fallback.
        try:
            preserve_link_failed_c_owners(
                work, output, rows, results, output / "gnu-ld-relink.log")
        except Exception as exc:
            # Postmortem collection must never conceal the actual GNU LD
            # rejection, even if a future diagnostic parser itself fails.
            (output / "link-failure-owner-diagnostic-error.txt").write_text(
                str(exc)[:4096])
        raise Rejected(f"original GNU Kbuild relink failed rc={linked.returncode}")
    # Re-enumerate *all* live original-Kbuild C owners after the link, not just
    # the preoverlay list. A new GCC-generated C object may not evade this
    # union check even if each previous MiniC .o remained byte-for-byte intact.
    census = verify_postlink_c_owners(out, output, data["target"], expected)
    verify_gnu_kbuild_commands(out, rows, data["gcc"], data["target"])
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
                  gnu_c_option_equivalence=data["gnu_c_option_equivalence"],
                  gnu_c_option_unqualified_owners=data["gnu_c_option_unqualified_owners"],
                  golden_c_owners=data["gnu_c_owners"],
                  minic_c_owners=data["gnu_c_owners"], gnu_c_fallback_count=0,
                  lanes=[], verdict="MINIC_QEMU_NOT_CERTIFIED")
    for profile in ("p0", "p1"):
        lane = oracle.run_lane(data["target"], qemu, image, initramfs, firmware,
                               profile, "2G", 240, boot_dir)
        lane["verdict"] = "MINIC_QEMU_RUNTIME_PASS"
        result["lanes"].append(lane)
    # A boot-complete receipt is NOT a GNU/MiniC runtime differential PASS.
    # Keep the persisted artifact fail-closed if either comparator fails or
    # the process is interrupted after only one lane has been compared.
    result["verdict"] = MINIC_PRE_DIFF_VERDICT
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
    # Publish the final verdict only after both independently verified GNU
    # semantic differential receipts exist; use a same-directory atomic
    # replacement so a failed/partial run never leaves a successful receipt.
    for profile in ("p0", "p1"):
        differential = json.loads(required(
            boot_dir / f"{profile}-differential.json").read_text())
        if (differential.get("verdict") != "GNU_MINIC_RUNTIME_SEMANTIC_MATCH" or
                differential.get("target") != data["target"] or
                differential.get("profile") != profile or
                differential.get("minic_image_sha256") != result["image_sha256"]):
            raise Rejected(f"missing authenticated {profile} runtime differential")
    result["verdict"] = MINIC_VERDICT
    temporary = receipt.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(result, sort_keys=True, indent=2) + "\n")
    os.replace(temporary, receipt)
    return result


def self_test() -> None:
    assert set(TARGETS) == {"x86_64", "aarch64", "loongarch64"}
    assert len(ALLOWED_CFLAGS) == 16
    assert len(LINUX_SHA) == 64
    link_summary = summarize_gnu_link_failures(
        "ld: mm/gup.o: undefined reference to `__compiletime_assert_707'\n"
        "ld: fs/exec.o: undefined reference to '__field_overflow'\n"
        "ld: fs/exec.o: undefined reference to `__compiletime_assert_707'\n"
        "ld: relocation truncated to fit: R_LARCH_B26 against `start_secondary'\n")
    assert link_summary["undefined_references"] == 3
    assert link_summary["undefined_symbols"] == 2
    assert link_summary["categories"] == {"compiletime_assert": 2, "object_bounds": 1}
    assert link_summary["truncated_relocations"] == {"R_LARCH_B26": 1}
    assert positional_c_sources([
        "aarch64-linux-gnu-gcc", "-E", "-P", "-include",
        "lib/vdso/gettimeofday.c", "-imacros", "generated/macros.c",
        "-O2", "arch/arm64/kernel/vdso/vgettimeofday.c",
        "-o", "vgettimeofday.i",
    ]) == [8]
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
        # AArch diagnostics classify real ELF relocation encodings, including
        # an undefined branch, without GNU LD or changing an ELF object.
        strtab = b"\0target_symbol\0__compiletime_assert_317\0"
        tiny = bytearray(512)
        tiny[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<HH", tiny, 16, 1, 183)
        struct.pack_into("<Q", tiny, 40, 256)
        struct.pack_into("<HH", tiny, 58, 64, 4)
        tiny[64:64 + len(strtab)] = strtab
        struct.pack_into("<IxxH", tiny, 128 + 24, 1, 0)
        struct.pack_into("<IxxH", tiny, 128 + 48,
                         strtab.index(b"__compiletime_assert_317"), 0)
        struct.pack_into("<QQq", tiny, 200, 0, (1 << 32) | 311, 0)
        struct.pack_into("<QQq", tiny, 224, 4, (2 << 32) | 283, 0)
        section_format = "<IIQQQQIIQQ"
        struct.pack_into(section_format, tiny, 256 + 64, 0, 3, 0, 0,
                         64, len(strtab), 0, 0, 1, 0)
        struct.pack_into(section_format, tiny, 256 + 128, 0, 2, 0, 0,
                         128, 72, 1, 0, 8, 24)
        struct.pack_into(section_format, tiny, 256 + 192, 0, 4, 0, 0,
                         200, 48, 2, 0, 8, 24)
        aarch_elf = Path(temp) / "aarch-reloc.o"
        aarch_elf.write_bytes(tiny)
        relocation = scan_aarch64_relocations(aarch_elf)
        assert relocation["got"] == 1 and relocation["undefined_branch"] == 1
        assert relocation["compiletime_assert_branch"] == 1
        assert relocation["got_samples"][0]["symbol"] == "target_symbol"
        struct.pack_into("<Q", tiny, 208, (1 << 32) | 1026)
        aarch_elf.write_bytes(tiny)
        assert scan_aarch64_relocations(aarch_elf)["plt"] == 1
        struct.pack_into("<Q", tiny, 208, (1 << 32) | 311)
        gnu_dir = Path(temp) / "aarch-original-gnu"
        mini_dir = Path(temp) / "aarch-minic-staged"
        for folder in (gnu_dir, mini_dir):
            (folder / "kernel").mkdir(parents=True)
            (folder / "kernel/fixmap.o").write_bytes(tiny)
        aarch_diag = Path(temp) / "aarch-got-plt-artifact"
        rows_aarch = [{"object": "kernel/fixmap.o"}]
        capture_aarch64_got_plt_owners(gnu_dir, mini_dir, aarch_diag,
                                      rows_aarch, False)
        summary = json.loads((aarch_diag / "aarch64-got-plt-diagnostic" /
                              "preoverlay.json").read_text())
        assert summary["verdict"] == "RELOCATION_DIAGNOSTIC_COMPLETE"
        assert summary["variants"]["minic-candidate"]["got_relocations"] == 1
        assert summary["variants"]["original-gnu"]["compiletime_assert_branch_relocations"] == 1
        (gnu_dir / "vmlinux.o").write_bytes(tiny)
        capture_aarch64_got_plt_owners(gnu_dir, mini_dir, aarch_diag,
                                      rows_aarch, True)
        summary = json.loads((aarch_diag / "aarch64-got-plt-diagnostic" /
                              "postlink.json").read_text())
        assert summary["variants"]["postlink-vmlinux-o"]["got_relocations"] == 1
        aarch_elf.write_bytes(b"invalid")
        try:
            scan_aarch64_relocations(aarch_elf)
        except ValueError:
            pass
        else:
            raise AssertionError("AArch ELF relocation diagnostic accepted invalid input")
        failures_checked += 4
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
        # On original-GNU *link* failure there are no failed MiniC C owners.
        # GNU LD can print only a basename; exactly one matching manifest
        # owner is safe, while duplicate basenames and aggregate/derived
        # `kvm_nvhe.o` must remain explicit unknown/ambiguous evidence.
        link_fixture = Path(temp) / "strict-link-failure.log"
        link_fixture.write_text(
            "ld: first_error_2.o:(.text+0x4): undefined reference to `guard_a'\n"
            "ld: first_error_1.o:(.text+0x8): undefined reference to `guard_b'\n"
            "ld: first_error_3.o:(.text+0xa): undefined reference to `asm_collision'\n"
            "ld: kernel/first_error_0.o:(.text+0xc): undefined reference to `guard_c'\n"
            "ld: kernel/first_error_1.o:(.text+0xe): undefined reference to `full_path_guard'\n"
            "ld: kvm_nvhe.o:(.text+0x10): undefined reference to `__kvm_nvhe_guard'\n")
        other_name = "other/first_error_1.o"
        other_cmd = first_work / "build/other/.first_error_1.o.cmd"
        other_cmd.parent.mkdir(parents=True)
        other_cmd.write_text("savedcmd := gcc original/other.i\n")
        other_frozen = first_work / "frozen/i/other/first_error_1.i"
        other_frozen.parent.mkdir(parents=True)
        other_frozen.write_text("/* ambiguous unrelated GNU C owner */\n")
        ambiguous_rows = [*failed_rows, {
            "object": other_name, "input": "i/other/first_error_1.i",
            "sha256": sha(other_frozen), "cmd_sha256": sha(other_cmd)}]
        all_gnu_names = sorted([row["object"] for row in ambiguous_rows] +
                               ["arch/asm/first_error_3.o"])
        gnu_evidence = first_work / "evidence"
        gnu_evidence.mkdir(parents=True)
        (gnu_evidence / "discovery.json").write_text(json.dumps(
            {"gnu_target_objects": len(all_gnu_names)}))
        (gnu_evidence / "gnu-target-objects.sha256").write_text("".join(
            f"{'a' * 64}  {name}\n" for name in all_gnu_names))
        passed = [{"object": row["object"], "status": "MINIC_S_GNU_AS_PASS",
                   "asm_sha256": None} for row in ambiguous_rows]
        link_output = Path(temp) / "strict-link-output"
        link_output.mkdir()
        staged_asm = link_output / "staged/kernel/first_error_2.s"
        staged_asm.parent.mkdir(parents=True)
        staged_asm.write_text(".text\n/* MiniC exact candidate S */\n")
        passed[2]["asm_sha256"] = sha(staged_asm)
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            link_report = preserve_link_failed_c_owners(
                first_work, link_output, ambiguous_rows, passed, link_fixture)
        assert "guard_a" not in stdout.getvalue()
        assert "original GNU C owner" not in stdout.getvalue()
        assert [item["owner"] for item in link_report["samples"]] == [
            "kernel/first_error_2.o", "kernel/first_error_0.o",
            "kernel/first_error_1.o"]
        assert [item["token"] for item in link_report["ambiguous_basenames"]] == [
            "first_error_1.o", "first_error_3.o"]
        assert link_report["unknown_non_c_owners"] == ["kvm_nvhe.o"]
        assert link_report["original_link_log_sha256"] == sha(link_fixture)
        assert (link_output / "link-failure-owner-diagnostics/kernel/first_error_2/"
                "original.gnu.i").read_bytes() == (
                    first_work / "frozen/i/kernel/first_error_2.i").read_bytes()
        assert (link_output / "link-failure-owner-diagnostics/kernel/first_error_1/"
                "original.gnu.i").is_file()
        assert not (link_output / "link-failure-owner-diagnostics/other/first_error_1").exists()
        assert not (link_output / "link-failure-owner-diagnostics/kernel/first_error_3").exists()
        assert not (link_output / "link-failure-owner-diagnostics/kvm_nvhe").exists()
        assert (link_output / "link-failure-owner-diagnostics/kernel/first_error_2/"
                "minic.s").read_bytes() == staged_asm.read_bytes()
        link_budget_out = Path(temp) / "strict-link-budget"
        link_budget_out.mkdir()
        budget_report = preserve_link_failed_c_owners(
            first_work, link_budget_out, ambiguous_rows, passed,
            link_fixture, limit=1, byte_budget=1)
        assert not budget_report["samples"] and len(budget_report["skipped"]) == 3
        assert all(item["reason"] == "byte_budget" for item in budget_report["skipped"])
        link_count_out = Path(temp) / "strict-link-count"
        link_count_out.mkdir()
        count_report = preserve_link_failed_c_owners(
            first_work, link_count_out, ambiguous_rows, passed, link_fixture, limit=1)
        assert count_report["sample_count"] == 1
        assert count_report["skipped"] == [
            {"owner": "kernel/first_error_0.o", "reason": "owner_count_budget"},
            {"owner": "kernel/first_error_1.o", "reason": "owner_count_budget"}]
        bad_rows = [dict(row) for row in ambiguous_rows]
        bad_rows[0]["object"] = "../malicious.o"
        bad_passed = [dict(item) for item in passed]
        bad_passed[0]["object"] = "../malicious.o"
        try:
            preserve_link_failed_c_owners(
                first_work, Path(temp) / "unsafe-owner-out", bad_rows, bad_passed, link_fixture)
            raise AssertionError("traversal escaped original GNU owner mapping")
        except Rejected:
            pass
        bad_rows = [dict(row) for row in ambiguous_rows]
        bad_rows[0]["sha256"] = "0" * 64
        try:
            preserve_link_failed_c_owners(
                first_work, Path(temp) / "bad-digest-out", bad_rows, passed, link_fixture)
            raise AssertionError("invalid GNU frozen sha was accepted")
        except Rejected:
            pass
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
                     "gnu_as_flags": [], "minic_flags": []}]
        assert verify_gnu_kbuild_commands(out, metadata, "gcc")[
            "gnu_c_option_equivalence"] == "NO_UNQUALIFIED_GNU_CODEGEN_OPTIONS"
        metadata[0]["gnu_as_flags"] = ["-mabi=lp64s"]
        try:
            verify_gnu_kbuild_commands(out, metadata, "gcc")
        except Rejected:
            failures_checked += 1
        else:
            raise AssertionError("forged original GNU AS flags escaped .cmd gate")
        metadata[0]["gnu_as_flags"] = []
        metadata[0]["minic_flags"] = ["-fshort-wchar"]
        try:
            verify_gnu_kbuild_commands(out, metadata, "gcc")
        except Rejected:
            failures_checked += 1
        else:
            raise AssertionError("forged original GNU C ABI flags escaped .cmd gate")
        metadata[0]["minic_flags"] = []
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
        if data["gnu_c_option_equivalence"] != "NO_UNQUALIFIED_GNU_CODEGEN_OPTIONS":
            # Preserve the successful link/QEMU evidence but never promote
            # ignored original GNU codegen options to strict equivalence.
            # This gate runs after the full campaign so it cannot hide the
            # current three-ISA link failures or their forensic artifacts.
            outcome["verdict"] = "MINIC_LINK_QEMU_RUNTIME_MATCH_GNU_C_OPTIONS_INCONCLUSIVE"
            print(f"THREE_ISA_MINIC_FULL_RUNTIME=INCONCLUSIVE target={a.target} "
                  f"owners={len(rows)}/{len(rows)} fallback_GNU_C=0 "
                  f"QEMU=P0_P1 GNU_diff=PASS option_equivalence=INCONCLUSIVE")
            rc = 2
        else:
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
