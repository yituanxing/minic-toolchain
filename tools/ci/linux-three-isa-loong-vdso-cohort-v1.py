#!/usr/bin/env python3
"""Diagnostic-only LoongArch Linux P0: GNU vDSO glue in an all-MiniC kernel.

Only the already-built, SHA-attested original GNU C object is substituted.
Neither this experiment nor its QEMU result can certify a full MiniC kernel.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


TARGET = "loongarch64"
OWNER = "arch/loongarch/kernel/vdso.o"
SCHEMA = "minic-loong-vdso-single-gnu-owner-cohort-v1"


def sibling(filename: str, label: str):
    import importlib.util
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(label, path)
    if spec is None or spec.loader is None:
        raise ValueError(f"missing official oracle: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def exact(path: Path) -> Path:
    if not path.is_file() or path.is_symlink() or not path.stat().st_size:
        raise ValueError(f"missing/unsafe original evidence: {path}")
    return path


def canonical_dir(path: Path) -> Path:
    path = path.absolute()
    if not path.is_dir() or path.is_symlink() or path.resolve(strict=True) != path:
        raise ValueError(f"missing/unsafe worktree: {path}")
    return path


def read(path: Path) -> dict:
    return json.loads(exact(path).read_text())


def write_new(path: Path, content: dict) -> None:
    if path.exists() or path.is_symlink():
        raise ValueError(f"refuse overwriting evidence: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(content, sort_keys=True, indent=2) + "\n")


def c_owner_hashes(work: Path, output: Path, phase: str) -> dict[str, str]:
    """Use the original GNU Kbuild object classifier, including live C owners."""
    candidate = sibling("linux-three-isa-minic-candidate-v1.py", "loong_census_candidate")
    golden = candidate.module("loong_census_golden", "linux-three-isa-golden-v1.py")
    common = candidate.module("loong_census_owners", "linux-runtime-gcc-full-c-objects-v1.py")
    out = work / "build"
    lines = []
    for obj in sorted(out.rglob("*.o")):
        if obj.is_symlink() or not obj.is_file():
            continue
        rel = obj.relative_to(out)
        parts = rel.parts
        if (parts and parts[0] in ("tools", "scripts")) or (
                len(parts) >= 3 and parts[0] == "arch" and parts[2] == "tools"):
            continue
        if golden.classify_object(obj, out, TARGET) == "main":
            lines.append(f"{digest(obj)}  {rel.as_posix()}\n")
    if phase not in ("original", "mixed", "restored"):
        raise ValueError("invalid diagnostic owner census phase")
    manifest = output / f"{phase}-target-elf64-objects.sha256"
    manifest.write_text("".join(lines))
    owners, _ = common.examine(out, manifest)
    return {name: digest(exact(out / name)) for name in owners}


def pin(args: argparse.Namespace) -> None:
    work = canonical_dir(args.gnu_work)
    output = args.output.absolute()
    if output.exists() or output.is_relative_to(work):
        raise ValueError("pin output must be new and outside GNU_WORK")
    candidate = sibling("linux-three-isa-minic-candidate-v1.py", "loong_pin_candidate")
    verifier = sibling("linux-three-isa-gnu-cache-verify-v1.py", "loong_pin_cache")
    verifier.verify_cache(TARGET, work, "loongarch64-linux-gnu-gcc-13", exact(args.gnu_receipt))
    golden = read(args.gnu_receipt)
    rows, data = candidate.candidate_inputs(
        TARGET, work, exact(args.minic), golden, exact(args.initramfs))
    if len(rows) != 2723 or [row["object"] for row in rows].count(OWNER) != 1:
        raise ValueError("unexpected frozen Loong C-owner cohort")
    source = exact(work / "build" / OWNER)
    candidate.elf_machine(source, 258)
    output.mkdir(parents=True)
    pinned = output / "pristine-gnu-vdso.o"
    shutil.copyfile(source, pinned)
    if digest(pinned) != digest(source):
        raise ValueError("GNU vDSO source moved during pin")
    metadata = {"schema": SCHEMA, "phase": "pinned-original-gnu-vdso",
                "target": TARGET, "diagnostic_only": True,
                "strict_mini_c_acceptance": False, "owner": OWNER,
                "owners": len(rows), "gnu_work": str(work),
                "original_manifest_sha256": digest(work / "frozen/manifest.json"),
                "original_gnu_vdso_sha256": digest(pinned),
                "original_gnu_image_sha256": golden["image_sha256"],
                "gnu_receipt_sha256": digest(args.gnu_receipt),
                "gnu_p0_log_sha256": digest(exact(args.gnu_receipt.parent / "p0.log")),
                "config_sha256": data["config_sha256"],
                "initramfs_sha256": digest(args.initramfs),
                "compiler_driver_sha256": data["compiler_driver_sha256"],
                "compiler_cc_sha256": data["compiler_cc_sha256"]}
    write_new(output / "pin.json", metadata)
    print(f"LOONG_VDSO_COHORT_PIN=PASS owners={len(rows)} gnu_vdso_sha={digest(pinned)} "
          "full_mini_c_acceptance=false")


def probe(args: argparse.Namespace) -> int:
    work = canonical_dir(args.gnu_work)
    pinned = canonical_dir(args.pinned)
    info = read(pinned / "pin.json")
    if (info.get("schema") != SCHEMA or info.get("phase") != "pinned-original-gnu-vdso"
            or info.get("gnu_work") != str(work) or info.get("owner") != OWNER
            or info.get("diagnostic_only") is not True):
        raise ValueError("foreign original-GNU vDSO pin")
    original_gnu_vdso = exact(pinned / "pristine-gnu-vdso.o")
    if digest(original_gnu_vdso) != info["original_gnu_vdso_sha256"]:
        raise ValueError("pinned GNU vDSO SHA drift")
    if digest(exact(work / "frozen/manifest.json")) != info["original_manifest_sha256"]:
        raise ValueError("original GNU input manifest drift")
    gnu = read(args.gnu_receipt)
    if (digest(args.gnu_receipt) != info["gnu_receipt_sha256"]
            or gnu.get("verdict") != "GNU_QEMU_P0_P1_PASS_NOT_MINIC"
            or digest(args.initramfs) != info["initramfs_sha256"]
            or digest(args.firmware) != gnu.get("firmware_sha256")):
        raise ValueError("GNU P0/P1 original oracle / Loong EFI pin drift")
    if (digest(exact(args.gnu_p0)) != info["gnu_p0_log_sha256"]
            or gnu["lanes"][0].get("log_sha256") != info["gnu_p0_log_sha256"]):
        raise ValueError("actual original GNU P0 semantic oracle log drift")
    candidate = sibling("linux-three-isa-minic-candidate-v1.py", "loong_probe_candidate")
    compiler_shas = candidate.compiler_pair(exact(args.minic))
    if any(compiler_shas[k] != info[k] for k in
           ("compiler_driver_sha256", "compiler_cc_sha256")):
        raise ValueError("producer compiler differs from GNU pin")
    census = read(args.census)
    if (census.get("verdict") != "POSTLINK_ALL_C_MINIC_SHA_PASS"
            or census.get("expected_c_owners") != info["owners"]
            or census.get("actual_c_owners") != info["owners"]):
        raise ValueError("official all-C MiniC SHA provenance unavailable")
    failed = exact(args.failed_p0).read_text(errors="replace")
    if ("Run /init as init process" not in failed or "Kernel panic" not in failed
            or "do_page_fault(): sending SIGSEGV to init" not in failed):
        raise ValueError("not the observed Loong init SIGSEGV frontier")
    trial = args.trial.absolute()
    if trial.exists() or trial.is_relative_to(work):
        raise ValueError("probe output must be new and outside GNU_WORK")
    trial.mkdir(parents=True)
    original = work / "build"
    backup = work / "build-before-loong-vdso-cohort"
    if backup.exists() or backup.is_symlink():
        raise ValueError("a previous original GNU Kbuild backup already exists")
    image_rel = "arch/loongarch/boot/vmlinux.efi"
    candidate_image = exact(original / image_rel)
    if digest(candidate_image) == info["original_gnu_image_sha256"]:
        raise ValueError("candidate EFI image is original GNU, not MiniC")
    shutil.copyfile(candidate_image, trial / "original-all-minic-candidate.efi")
    record = {"schema": SCHEMA, "phase": "one-original-GNU-vdso-mixed-cohort",
              "diagnostic_only": True, "strict_mini_c_acceptance": False,
              "qualification": "MIXED_NOT_CERTIFIED",
              "target": TARGET, "owner": OWNER, "expected_mini_c_owners": info["owners"] - 1,
              "expected_gnu_owners": 1, "gnu_source_image_sha256": info["original_gnu_image_sha256"],
              "all_mini_c_candidate_image_sha256": digest(trial / "original-all-minic-candidate.efi"),
              "original_build_restored": False, "verdict": "DIAGNOSTIC_INCOMPLETE"}
    moved = False
    try:
        canonical_dir(original)
        baseline_owners = c_owner_hashes(work, trial, "original")
        if len(baseline_owners) != info["owners"]:
            raise ValueError("official final candidate C-owner universe unexpectedly changed")
        original.rename(backup)
        moved = True
        # Kbuild .cmd references the exact original O= path: preserve it.
        subprocess.run(["cp", "-a", "--reflink=auto", str(backup), str(original)], check=True)
        if (original / ".config").stat().st_ino == (backup / ".config").stat().st_ino:
            raise ValueError("Kbuild experiment shares original inode")
        destination = exact(original / OWNER)
        record["original_mini_c_vdso_sha256"] = digest(destination)
        shutil.copyfile(original_gnu_vdso, destination)
        os.utime(destination, None)
        if digest(destination) != info["original_gnu_vdso_sha256"]:
            raise ValueError("single-object GNU vDSO substitution failed")
        command = ["make", "-C", str(work / "linux-6.6.143"), "O=" + str(original),
                   "ARCH=loongarch", "CROSS_COMPILE=loongarch64-linux-gnu-",
                   "CC=loongarch64-linux-gnu-gcc-13", "-j4", "vmlinuz.efi"]
        env = dict(os.environ, KBUILD_BUILD_USER="minic", KBUILD_BUILD_HOST="minic-ci",
                   KBUILD_BUILD_TIMESTAMP="Thu Jan  1 00:00:00 UTC 1970")
        run = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, env=env, timeout=args.link_timeout)
        (trial / "gnu-kbuild-one-gnu-vdso-link.log").write_text(run.stdout)
        record.update(link_rc=run.returncode, link_command=command,
                      link_log_sha256=digest(trial / "gnu-kbuild-one-gnu-vdso-link.log"),
                      kbuild_logged_cc_owners=re.findall(r"(?m)^\s*CC\s+(\S+\.o)\s*$", run.stdout),
                      relink_logged=bool(re.search(r"\bLD\b.*(?:vmlinux|Image)|\bAR\b.*built-in", run.stdout)),
                      postlink_vdso_sha256=digest(exact(original / OWNER)))
        record["extra_gnu_cc_owners"] = sorted(set(record["kbuild_logged_cc_owners"]) - {OWNER})
        mixed_owners = c_owner_hashes(work, trial, "mixed")
        record["postlink_c_owner_count"] = len(mixed_owners)
        record["other_c_owner_drifts"] = sorted(
            name for name in set(baseline_owners) | set(mixed_owners)
            if name != OWNER and baseline_owners.get(name) != mixed_owners.get(name))
        record["single_owner_partition_preserved"] = (
            len(mixed_owners) == info["owners"] and not record["other_c_owner_drifts"])
        record["postlink_vdso_origin"] = (
            "ORIGINAL_GNU_SHA" if record["postlink_vdso_sha256"] == info["original_gnu_vdso_sha256"]
            else "REGENERATED_GNU_IF_CC_LOGGED" if OWNER in record["kbuild_logged_cc_owners"]
            else "UNRECOGNIZED_OWNER_DRIFT")
        if run.returncode or not record["relink_logged"]:
            record["verdict"] = "DIAGNOSTIC_GNU_LINK_FAIL_OR_NO_RELINK"
            return 2
        if record["postlink_vdso_origin"] == "UNRECOGNIZED_OWNER_DRIFT":
            record["verdict"] = "DIAGNOSTIC_UNAUTHENTICATED_VDSO_DRIFT"
            return 2
        if not record["single_owner_partition_preserved"]:
            record["verdict"] = "DIAGNOSTIC_INCONCLUSIVE_OTHER_C_OWNER_DRIFT"
            return 2
        # Preserve the actual mixed EFI bytes even when a later diagnostic
        # guard rules out causal interpretation or a QEMU run.
        target_image = exact(original / image_rel)
        shutil.copyfile(target_image, trial / "mixed-one-gnu-vdso.efi")
        record["mixed_image_sha256"] = digest(target_image)
        if record["mixed_image_sha256"] in (record["all_mini_c_candidate_image_sha256"],
                                             info["original_gnu_image_sha256"]):
            record["verdict"] = "DIAGNOSTIC_INCONCLUSIVE_UNCHANGED_IMAGE"
            return 2
        if record["extra_gnu_cc_owners"]:
            record["verdict"] = "DIAGNOSTIC_INCONCLUSIVE_ADDITIONAL_GNU_CC"
            return 2
        oracle = sibling("linux-three-isa-gnu-qemu-v1.py", "loong_probe_qemu")
        diff = sibling("linux-four-isa-runtime-diff-v1.py", "loong_probe_diff")
        boot = trial / "boot"
        boot.mkdir()
        try:
            lane = oracle.run_lane(TARGET, oracle.executable("qemu-system-loongarch64"),
                                   target_image, args.initramfs, args.firmware,
                                   "p0", "2G", args.qemu_timeout, boot)
            record["p0_lane"] = lane
            record["semantic_events"] = diff.compare(
                exact(args.gnu_p0).read_text(errors="replace"),
                (boot / "p0.log").read_text(errors="replace"), "p0", "6.6.143")
            record["verdict"] = "DIAGNOSTIC_MIXED_P0_MATCHES_GNU_NOT_ACCEPTANCE"
            return 0
        except (oracle.CertificationError, diff.RuntimeMismatch) as exc:
            record["p0_error"] = str(exc)
            record["verdict"] = "DIAGNOSTIC_MIXED_P0_FAIL_OR_SEMANTIC_MISMATCH"
            return 2
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        record["error"] = str(exc)
        record["verdict"] = "DIAGNOSTIC_INCOMPLETE_OR_UNSAFE"
        return 2
    finally:
        if moved:
            if original.exists():
                shutil.rmtree(original)
            backup.rename(original)
            record["original_build_restored"] = True
            if digest(exact(original / image_rel)) != record["all_mini_c_candidate_image_sha256"]:
                raise ValueError("official all-MiniC candidate EFI changed after diagnostic")
            record["original_c_owners_restored"] = (
                c_owner_hashes(work, trial, "restored") == baseline_owners)
            if not record["original_c_owners_restored"]:
                raise ValueError("original 2723 MiniC C objects changed after diagnostic")
        write_new(trial / "diagnostic-receipt.json", record)
        print(f"LOONG_VDSO_COHORT={record['verdict']} "
              f"owner_origin={record.get('postlink_vdso_origin', 'NOT_RUN')} "
              f"build_restored={record['original_build_restored']} "
              "diagnostic_only=true strict_acceptance=false qualification=MIXED_NOT_CERTIFIED")
        print("LOONG_VDSO_COHORT_EXTRA_GNU_CC=" + json.dumps({
            "count": len(record.get("extra_gnu_cc_owners", [])),
            "owners_first20": record.get("extra_gnu_cc_owners", [])[:20],
            "other_c_owner_sha_drifts": len(record.get("other_c_owner_drifts", [])),
            "full_receipt": str(trial / "diagnostic-receipt.json")},
            sort_keys=True, separators=(",", ":")))


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    cmd = p.add_subparsers(dest="command", required=True)
    pin_cmd = cmd.add_parser("pin")
    pin_cmd.add_argument("--gnu-work", type=Path, required=True)
    pin_cmd.add_argument("--gnu-receipt", type=Path, required=True)
    pin_cmd.add_argument("--initramfs", type=Path, required=True)
    pin_cmd.add_argument("--minic", type=Path, required=True)
    pin_cmd.add_argument("--output", type=Path, required=True)
    probe_cmd = cmd.add_parser("probe")
    for option in ("gnu-work", "gnu-receipt", "initramfs", "minic", "pinned", "firmware",
                   "gnu-p0", "census", "failed-p0", "trial"):
        probe_cmd.add_argument("--" + option, type=Path, required=True)
    probe_cmd.add_argument("--link-timeout", type=int, default=600)
    probe_cmd.add_argument("--qemu-timeout", type=int, default=180)
    args = p.parse_args(argv)
    try:
        if args.command == "pin":
            pin(args)
            return 0
        if not 30 <= args.link_timeout <= 1800 or not 30 <= args.qemu_timeout <= 600:
            raise ValueError("diagnostic link/QEMU timeout outside safe bounds")
        return probe(args)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"LOONG_VDSO_COHORT_DIAGNOSTIC=FAIL {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
