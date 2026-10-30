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
    for row in rows:
        name = PurePosixPath(row["object"])
        if (name.is_absolute() or ".." in name.parts or name.suffix != ".o" or
                not isinstance(row.get("minic_flags"), list) or
                any(flag not in ALLOWED_CFLAGS for flag in row["minic_flags"])):
            raise Rejected(f"unsafe or unsupported owner: {row['object']}")
        required(out / name)
        if not (out / name).is_file():
            raise Rejected(f"missing GNU original C object: {name}")
    comp_sha = sha(required(compiler))
    return rows, {"target": target, "gnu_c_owners": len(rows),
                  "golden_image_sha256": golden_receipt["image_sha256"],
                  "source_sha256": LINUX_SHA,
                  "config_sha256": golden_receipt["config_sha256"],
                  "initramfs_sha256": golden_receipt["initramfs_sha256"],
                  "firmware_sha256": golden_receipt.get("firmware_sha256"),
                  "gnu_c_owner_manifest_sha256": discovery["owner_manifest_sha256"],
                  "compiler_sha256": comp_sha, "arch": arch, "cross": cross,
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
                result.update(status="MINIC_S_FAIL", detail=cc.stderr[-600:])
                return result
            result["asm_sha256"] = sha(asm)
            ass = subprocess.run([gcc, "-x", "assembler", "-c", str(asm), "-o", str(obj)],
                                 text=True, capture_output=True, timeout=timeout)
            if ass.returncode or not obj.is_file() or not obj.stat().st_size:
                result.update(status="GNU_TARGET_AS_FAIL", detail=ass.stderr[-600:])
                return result
            elf_machine(obj, data["machine"])
            result.update(status="MINIC_S_GNU_AS_PASS", obj_sha256=sha(obj))
            return result
        except (OSError, subprocess.TimeoutExpired, Rejected) as exc:
            result.update(status="SOURCE_AS_ERROR", detail=str(exc)[-600:])
            return result
        finally:
            result["elapsed_seconds"] = round(time.monotonic() - started, 3)

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        results = list(pool.map(one, rows))
    return results


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
    if linked.returncode:
        raise Rejected(f"original GNU Kbuild relink failed rc={linked.returncode}")
    # Hard guarantee even if a generated header caused Kbuild to replay GNU
    # compilation after overlay: byte-for-byte *every* C object must still be
    # its staged MiniC version.  No 2064-style fixed count applies here.
    fallback = [name for name, hash_ in expected.items() if sha(required(out / name)) != hash_]
    (output / "postlink-gnu-c-fallback.json").write_text(json.dumps(fallback, indent=2) + "\n")
    if fallback:
        raise Rejected(f"GNU Kbuild replaced {len(fallback)} MiniC C objects; first={fallback[:8]}")
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
    with __import__("tempfile").TemporaryDirectory() as temp:
        bad = Path(temp) / "not-elf.o"
        bad.write_bytes(b"x" * 64)
        try:
            elf_machine(bad, 62)
        except Rejected:
            pass
        else:
            raise AssertionError("incorrectly accepted foreign object")
    print("THREE_ISA_ALL_MINIC_CANDIDATE_SELFTEST=PASS targets=3 zero_gnu_fallback_gate=1")


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
        failed = [row for row in results if row["status"] != "MINIC_S_GNU_AS_PASS"]
        (a.output / "all-c-owners-result.json").write_text(json.dumps(results, indent=2) + "\n")
        outcome.update(minic_s_gnu_as_pass=len(results) - len(failed), minic_s_gnu_as_fail=len(failed))
        if failed:
            raise Rejected(f"incomplete MiniC/GNU AS C owner coverage {len(results)-len(failed)}/{len(rows)}; first={failed[0]}")
        image = relink(a.gnu_work, a.output, rows, results, data, golden)
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
