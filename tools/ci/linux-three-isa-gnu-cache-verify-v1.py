#!/usr/bin/env python3
"""Fail-closed verification of a restored three-ISA original GNU golden tree.

This is cache/input authentication, not a GNU boot or MiniC acceptance claim.
It reuses canonical GNU object discovery, frozen-input verification, and the
candidate's original Kbuild-command verifier. It never edits the restored tree.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile


def script_module(name: str, filename: str):
    import importlib.util
    path = Path(__file__).with_name(filename)
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ValueError(f"missing canonical verifier: {path}")
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def exact_file(root: Path, path: Path, *, allow_empty: bool = False) -> Path:
    """Reject a symlink in *any* component, not only at the final filename."""
    root = root.absolute()
    path = path.absolute()
    if (not path.is_relative_to(root) or
            ".." in path.relative_to(root).parts or
            not path.resolve(strict=False).is_relative_to(root)):
        raise ValueError(f"file escaped golden root: {path}")
    for part in (root, *(root / Path(*path.relative_to(root).parts[:i])
                         for i in range(1, len(path.relative_to(root).parts) + 1))):
        if part.is_symlink():
            raise ValueError(f"symlink in golden input path: {part}")
    if not path.is_file() or (not allow_empty and path.stat().st_size == 0):
        raise ValueError(f"missing or empty golden input: {path}")
    return path


def external_receipt_file(path: Path) -> Path:
    """QEMU's separately cached runtime output need not reside in GNU_WORK.

    Reject parent aliases, links at any level, and empty/nonregular objects.
    The receipt's content is separately cross-checked against the GNU root.
    """
    path = path.absolute()
    if ".." in path.parts or any(part.is_symlink() for part in (path, *path.parents)):
        raise ValueError(f"unsafe external GNU QEMU receipt/log path: {path}")
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"missing external GNU QEMU receipt/log: {path}")
    return path


def receipt_sha(path: Path, expected_file: Path, root: Path) -> str:
    """Check sha256sum's pinned filename too: it is an O= cache identity."""
    text = exact_file(root, path).read_text().strip()
    match = re.fullmatch(r"([0-9a-f]{64})  (.+)", text)
    if not match:
        raise ValueError(f"invalid original GNU sha256sum receipt: {path}")
    recorded = Path(match.group(2))
    resolved = recorded if recorded.is_absolute() else root / recorded
    if resolved.absolute() != expected_file.absolute():
        raise ValueError(f"stale GNU O=/source path in SHA receipt: {path}")
    return match.group(1)


def within_gnu_work(root: Path, out: Path, name: str) -> Path:
    if not name or any(ch in name for ch in ("\x00", "\n", "\r")):
        raise ValueError("invalid Kbuild path")
    candidate = Path(name)
    joined = candidate if candidate.is_absolute() else out / candidate
    normalized = joined.resolve(strict=False)
    if not normalized.is_relative_to(root):
        raise ValueError(f"stale/foreign GNU Kbuild O= path: {name}")
    # A missing temporary GNU output is okay, but no parent can be a symlink.
    part = root
    for component in normalized.relative_to(root).parts[:-1]:
        part /= component
        if part.is_symlink():
            raise ValueError(f"symlink GNU Kbuild output directory: {part}")
    return normalized


def verify_cache(target: str, work: Path, gcc: str, receipt: Path | None = None,
                 expected_source_sha: str | None = None,
                 minimum: int = 1000) -> dict:
    golden = script_module("minic_cache_golden", "linux-three-isa-golden-v1.py")
    frozen_oracle = script_module("minic_cache_freeze", "linux-three-isa-freeze-i-v1.py")
    candidate = script_module("minic_cache_candidate", "linux-three-isa-minic-candidate-v1.py")
    if target not in golden.MACHINES or minimum < 1:
        raise ValueError("unsupported three-ISA target/minimum")
    work = work.absolute()
    if work.is_symlink() or not work.is_dir() or work.resolve(strict=True) != work:
        raise ValueError("GNU_WORK must be an existing canonical nonsymlink directory")
    out, evidence, frozen = work / "build", work / "evidence", work / "frozen"
    for directory in (out, evidence, frozen, work / "linux-6.6.143"):
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError(f"missing/unsafe GNU_WORK directory: {directory}")
    exact_file(work, work / "linux-6.6.143/Makefile")
    release = (work / "linux-6.6.143/Makefile").read_text()
    for prefix in ("VERSION = 6", "PATCHLEVEL = 6", "SUBLEVEL = 143"):
        if prefix not in release:
            raise ValueError("restored source tree is not Linux 6.6.143")
    exact_file(work, out / ".config")
    tar = exact_file(work, work / "linux-6.6.143.tar.xz")
    src_sha = (expected_source_sha or candidate.LINUX_SHA).lower()
    if not re.fullmatch(r"[0-9a-f]{64}", src_sha):
        raise ValueError("invalid expected Linux source identity")
    if (candidate.sha(tar) != src_sha or
            receipt_sha(evidence / "linux-source.sha256", tar, work) != src_sha or
            receipt_sha(evidence / "config.sha256", out / ".config", work) !=
            candidate.sha(out / ".config")):
        raise ValueError("stale GNU original Linux source/config SHA")
    discovery_path = exact_file(work, evidence / "discovery.json")
    discovery = json.loads(discovery_path.read_text())
    if (discovery.get("target") != target or
            discovery.get("verdict") != "GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC" or
            discovery.get("config_profile") != "broad" or
            discovery.get("config_sha256") != candidate.sha(out / ".config") or
            discovery.get("gnu_c_owners", 0) < minimum):
        raise ValueError("GNU discovery profile, target or config identity changed")
    image_name = discovery.get("kernel")
    if not isinstance(image_name, str) or not image_name:
        raise ValueError("missing canonical GNU linked kernel")
    image_path = PurePosixPath(image_name)
    if image_path.is_absolute() or ".." in image_path.parts:
        raise ValueError("unsafe GNU linked kernel path")
    kernel = exact_file(work, out / image_name)
    if candidate.sha(kernel) != discovery.get("kernel_sha256"):
        raise ValueError("stale original GNU Image SHA")
    metadata = {}
    for raw in exact_file(work, evidence / "build-contract.txt").read_text().splitlines():
        key, sep, value = raw.partition("=")
        if not sep or key in metadata:
            raise ValueError("invalid original GNU build-contract")
        metadata[key] = value
    target_arch = {"x86_64": "x86", "aarch64": "arm64",
                   "loongarch64": "loongarch"}[target]
    if any(metadata.get(key) != value for key, value in {
            "source": "linux-6.6.143", "target": target, "arch": target_arch,
            "gnu_cc": Path(gcc).name, "image": image_name,
            "profile": "broad",
            "verdict": "GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC"}.items()):
        raise ValueError("stale original GNU build-contract target/source/O= identity")
    for obj in out.rglob("*.o"):
        if obj.is_symlink():
            raise ValueError(f"symlink GNU ELF object: {obj}")
    # Re-census into an isolated temporary evidence directory.  This calls the
    # canonical host/compat/ELF C-owner classifier instead of copying it here;
    # comparison detects extra, removed, renamed, or changed ELF objects.
    with tempfile.TemporaryDirectory(prefix="minic-gnu-cache-census-") as temp:
        reborn = Path(temp)
        expected = golden.census(out, target, image_name, reborn, minimum, "broad")
        manifest_names = (
            "gnu-target-objects.sha256", "gnu-compat-elf32-objects.sha256",
            "separate-host-build-objects.txt", "separate-foreign-elf64-objects.sha256",
            "separate-x86-elf32-boot-domain.txt", "separate-elf32-compat-owners.txt",
            "separate-elf32-compat-objects.tsv", "separate-elf32-compat-excluded.tsv",
            "full-c-owners.txt", "excluded.tsv", "discovery.json")
        for name in manifest_names:
            authentic = exact_file(work, evidence / name, allow_empty=True)
            if authentic.read_bytes() != (reborn / name).read_bytes():
                raise ValueError(f"restored GNU discovery/ELF owner drift: {name}")
    # The canonical checker proves frozen .i identity, exact seven-shard owner
    # ordering, GNU tool identity/version and per-input SHA. Its counterpart
    # checks every *current* .o.cmd against the recorded per-owner SHA and ABI.
    rows = json.loads(exact_file(work, frozen / "manifest.json").read_text()).get("rows")
    if not isinstance(rows, list) or len(rows) != expected["gnu_c_owners"]:
        raise ValueError("incomplete frozen original C-owner rows")
    for row in rows:
        name = row.get("object")
        if not isinstance(name, str):
            raise ValueError("invalid frozen GNU C owner")
        owner = PurePosixPath(name)
        if owner.is_absolute() or ".." in owner.parts or owner.suffix != ".o":
            raise ValueError("unsafe frozen C owner")
        exact_file(work, out / owner)
        exact_file(work, out / owner.parent / ("." + owner.name + ".cmd"))
        exact_file(work, frozen / row["input"])
        text = (out / owner.parent / ("." + owner.name + ".cmd")).read_text()
        _, _, _, gnu_output = frozen_oracle.gnu_argv(
            text, gcc, name, Path("unused.i"), target)
        if gnu_output != row.get("gnu_output"):
            raise ValueError(f"changed original GNU Kbuild output: {name}")
        output = within_gnu_work(work, out, gnu_output)
        if not output.is_relative_to(out):
            raise ValueError(f"GNU -o escaped Kbuild O= directory: {name}")
        src = re.search(r"^source_[^\n]*\s*:=\s*(\S+\.c)\s*$", text, re.M)
        if src is not None:
            source_path = within_gnu_work(work, out, src.group(1))
            if not (source_path.is_relative_to(out) or
                    source_path.is_relative_to(work / "linux-6.6.143")):
                raise ValueError(f"GNU source path is not original Kbuild/source: {name}")
    frozen_oracle.verify(evidence, frozen, target, gcc)
    options = candidate.verify_gnu_kbuild_commands(out, rows, gcc, target)
    if receipt is not None:
        receipt_path = external_receipt_file(receipt)
        oracle = json.loads(receipt_path.read_text())
        qemu = script_module("minic_cache_qemu", "linux-three-isa-gnu-qemu-v1.py")
        if (oracle.get("schema") != "minic-three-isa-gnu-qemu-v1" or
                oracle.get("verdict") != candidate.GNU_VERDICT or
                oracle.get("target") != target or
                oracle.get("source_sha256") != src_sha or
                oracle.get("config_sha256") != expected["config_sha256"] or
                oracle.get("gnu_c_owners") != len(rows) or
                oracle.get("gnu_c_owner_manifest_sha256") !=
                    expected["owner_manifest_sha256"] or
                oracle.get("gnu_linked_kernel_sha256") != expected["kernel_sha256"]):
            raise ValueError("GNU P0/P1 receipt not bound to restored golden")
        # EFI boot image can differ from the linked vmlinux discovery image.
        boot_image = qemu.TARGETS[target]["image"]
        boot_file = exact_file(work, out / boot_image)
        if (candidate.sha(boot_file) != oracle.get("image_sha256") or
                Path(oracle.get("boot_image", "")).absolute() != boot_file):
            raise ValueError("GNU boot Image differs from QEMU receipt")
        initramfs_path = external_receipt_file(Path(oracle.get("initramfs", "")))
        if candidate.sha(initramfs_path) != oracle.get("initramfs_sha256"):
            raise ValueError("original GNU QEMU initramfs SHA mismatch")
        lanes = oracle.get("lanes")
        if (not isinstance(lanes, list) or len(lanes) != 2 or
                [row.get("lane") for row in lanes if isinstance(row, dict)] !=
                    ["p0", "p1"]):
            raise ValueError("fake/incomplete GNU P0/P1 QEMU receipt lanes")
        for lane in lanes:
            name = lane["lane"]
            logfile = external_receipt_file(Path(lane.get("log", "")))
            if (logfile != receipt_path.parent / f"{name}.log" or
                    candidate.sha(logfile) != lane.get("log_sha256") or
                    lane.get("verdict") != "GNU_QEMU_RUNTIME_PASS" or
                    lane.get("exit_code") != 0 or lane.get("timed_out") is not False):
                raise ValueError(f"fake/stale GNU QEMU {name} lane receipt")
            try:
                qemu.validate_log(logfile.read_text(errors="replace"), name,
                                  qemu.TARGETS[target]["console"])
            except qemu.CertificationError as exc:
                raise ValueError(f"GNU QEMU {name} log did not prove P0/P1: {exc}") from exc
    return {
        "verdict": "GNU_CACHE_RESTORED_VERIFIED_BUILD_ONLY_NOT_RUNTIME_OR_MINIC",
        "target": target, "owners": len(rows),
        "gnu_elf_objects": expected["gnu_target_objects"],
        "source_sha256": src_sha, "config_sha256": expected["config_sha256"],
        "kernel_sha256": expected["kernel_sha256"],
        "owner_manifest_sha256": expected["owner_manifest_sha256"],
        "frozen_manifest_sha256": candidate.sha(frozen / "manifest.json"),
        "qemu_receipt_checked": receipt is not None,
        **options,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("x86_64", "aarch64", "loongarch64"), required=True)
    parser.add_argument("--gnu-work", type=Path, required=True)
    parser.add_argument("--gcc", required=True)
    parser.add_argument("--gnu-receipt", type=Path)
    parser.add_argument("--min-c-objects", type=int, default=1000)
    args = parser.parse_args()
    try:
        result = verify_cache(args.target, args.gnu_work, args.gcc,
                              args.gnu_receipt, minimum=args.min_c_objects)
    except (OSError, ValueError, AssertionError, KeyError, TypeError) as exc:
        print(f"GNU_THREE_ISA_CACHE=FAIL {exc}", file=sys.stderr)
        return 2
    print("GNU_THREE_ISA_CACHE=PASS " +
          f"target={result['target']} owners={result['owners']} " +
          f"GNU_objects={result['gnu_elf_objects']} " +
          f"QEMU_receipt_checked={int(result['qemu_receipt_checked'])} " +
          "MiniC_certified=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
