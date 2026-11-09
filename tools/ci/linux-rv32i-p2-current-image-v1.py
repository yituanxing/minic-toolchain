#!/usr/bin/env python3
"""Fail-closed P2 admission for the immutable, full-current RV32 Linux Image.

The P2 consumer never builds a MiniC C owner or relinks Linux.  The source
and GNU provenance artifacts must come from the same completed seven-shard
P0/P1 producer, with all 1741 original C owners and twelve genuine QEMU P1
receipts.  Historical mixed-epoch 1738+3 sources are deliberately ineligible.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
from pathlib import Path, PurePosixPath
import re


@dataclass(frozen=True)
class Contract:
    producer_run: str = "42442670106"
    producer_sha: str = "314ff03b82d98d1149b340967a9ca47deb416453"
    image_sha256: str = "93ad6eeb4188057a790d68a07ecc4625ddcd4dc5f676e3b7366f7066f4815749"
    config_sha256: str = "4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c"
    linux_source_sha256: str = "dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932"
    owners: int = 1741
    header_replays: int = 3
    p1_boots: int = 12


PINNED = Contract()
HASH = re.compile(r"[0-9a-f]{64}\Z")
MANIFEST = re.compile(r"([0-9a-f]{64})  minic/(.+\.o)\Z")
REPLAY = re.compile(r"([0-9a-f]{64})  (.+\.o)\Z")


def fail(detail: str) -> None:
    raise ValueError("RV32I_P2_CURRENT_IMAGE=FAIL " + detail)


def checked_file(root: Path, relative: str) -> Path:
    path = root
    for part in PurePosixPath(relative).parts:
        if part in ("", ".", "..") or part.startswith("/"):
            fail("unsafe_input_path=" + relative)
        path = path / part
        if path.is_symlink():
            fail("symlink_input=" + relative)
    if not path.is_file():
        fail("missing_input=" + relative)
    return path


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def text_file(root: Path, relative: str) -> str:
    return checked_file(root, relative).read_text(encoding="utf-8")


def properties(value: str, where: str) -> dict[str, str]:
    result = {}
    for line in value.splitlines():
        if "=" not in line:
            fail("bad_property_line=" + where)
        key, val = line.split("=", 1)
        if key in result or not key or not val:
            fail("duplicate_or_empty_property=" + where + ":" + key)
        result[key] = val
    return result


def owners_from_text(value: str, label: str, expected: int) -> list[str]:
    lines = [line for line in value.splitlines() if line and not line.startswith("#")]
    if len(lines) != expected or len(set(lines)) != expected:
        fail("wrong_owner_count=" + label)
    for owner in lines:
        path = PurePosixPath(owner)
        if (path.is_absolute() or len(path.parts) < 2 or
                any(piece in ("", ".", "..") for piece in path.parts) or
                not owner.endswith(".o")):
            fail("invalid_owner=" + label + ":" + owner)
    return lines


def verify(image_root: Path, gnu_root: Path, contract: Contract = PINNED) -> str:
    image = checked_file(image_root, "out/arch/riscv/boot/Image")
    if digest(image) != contract.image_sha256:
        fail("immutable_Image_SHA_mismatch")

    original = checked_file(gnu_root, "out/.config")
    if digest(original) != contract.config_sha256:
        fail("original_GNU_Kconfig_SHA_mismatch")
    baseline = properties(text_file(gnu_root, "provenance/gcc-baseline.txt"),
                          "GNU_baseline")
    for key, value in {
        "schema": "linux-gnu-exact-config-v1",
        "linux_release": "6.6.143",
        "linux_archive_sha256": contract.linux_source_sha256,
        "elf_class": "ELF32", "isa": "rv32i", "abi": "ilp32",
        "config_sha256": contract.config_sha256,
        "c_owner_count": str(contract.owners),
    }.items():
        if baseline.get(key) != value:
            fail("original_GNU_profile_mismatch=" + key)

    identity = properties(text_file(image_root, "current-qemu-p1/source-identity.txt"),
                          "P1_source_identity")
    if identity != {
        "commit": contract.producer_sha,
        "image_sha256": contract.image_sha256,
        "producer_run": contract.producer_run,
    }:
        fail("foreign_or_mixed_P1_producer")

    link_receipt = text_file(image_root, "merged/union-result.txt")
    expected_link = ("RV32I_UNION_LINK=PASS ELF32=1 full_MiniC_C=true "
                     "original_GNU_Kbuild=true image_sha256=" +
                     contract.image_sha256 + " QEMU_P1=NOT_RUN")
    if link_receipt.strip() != expected_link or "mixed_epochs=true" in link_receipt:
        fail("not_full_current_MiniC_GNU_link")
    merged_identity = text_file(image_root, "merged/minic-identity.txt").strip()
    if not HASH.fullmatch(merged_identity):
        fail("invalid_seven_shard_identity")
    elf = text_file(image_root, "merged/minic-vmlinux-elf.txt")
    if not re.search(r"Class:\s+ELF32\b", elf) or not re.search(r"Machine:\s+RISC-V\b", elf):
        fail("no_GNU_linked_ELF32_RISCV")
    p0 = text_file(image_root, "current-qemu-p0/qemu-serial.log")
    if "RV32I_LINUX_PID1=PASS syscall=getpid,write elf=ELF32 abi=ilp32" not in p0:
        fail("current_P0_real_QEMU_PID1_oracle_missing")

    expected_owners = owners_from_text(
        text_file(gnu_root, "provenance/full-c-objects.txt"),
        "original_GNU_C_owners", contract.owners)
    rows = text_file(image_root, "merged/minic-manifest.sha256").splitlines()
    if len(rows) != contract.owners:
        fail("incomplete_seven_shard_C_manifest")
    mini_owners = []
    for row in rows:
        match = MANIFEST.fullmatch(row)
        if match is None:
            fail("bad_MiniC_C_manifest_row")
        mini_owners.append(match.group(2))
    if mini_owners != expected_owners:
        fail("MiniC_C_owners_not_exact_original_GNU_universe")

    affected = owners_from_text(
        text_file(image_root, "merged/header-affected-objects.txt"),
        "header_replay_owners", contract.header_replays)
    replay = text_file(image_root, "merged/header-replay-hashes.sha256").splitlines()
    replay_owners = []
    for row in replay:
        match = REPLAY.fullmatch(row)
        if match is None or not HASH.fullmatch(match.group(1)):
            fail("bad_generated_header_MiniC_replay_digest")
        replay_owners.append(match.group(2))
    if len(replay_owners) != contract.header_replays or set(replay_owners) != set(affected):
        fail("unproven_generated_header_replay_C_owners")
    if not set(affected) <= set(expected_owners):
        fail("foreign_generated_header_replay_C_owner")

    rows = text_file(image_root, "current-qemu-p1/p1-repeats.tsv").splitlines()
    if rows[0:1] != ["attempt\timage_sha256\tverdict"] or len(rows) != contract.p1_boots + 1:
        fail("incomplete_P1_twelve_boot_manifest")
    for attempt, row in enumerate(rows[1:], 1):
        if row != f"{attempt}\t{contract.image_sha256}\tPASS":
            fail("P1_boot_manifest_failure=" + str(attempt))
        trial = f"current-qemu-p1/trials/{attempt}"
        provenance = properties(text_file(image_root, trial + "/provenance.txt"),
                                "P1_trial_" + str(attempt))
        if provenance.get("image_sha256") != contract.image_sha256 or provenance.get("qemu_status") != "0":
            fail("unproven_P1_QEMU_trial=" + str(attempt))
        serial = text_file(image_root, trial + "/qemu-serial.log")
        for marker in (
            "RUNTIME_V2_SYSCALL_END pass=12 fail=0",
            "RV32I_LINUX_P1=PASS syscalls=12 child=ELF32 ilp32=true execve=true",
            "reboot: Power down",
        ):
            if marker not in serial:
                fail("P1_serial_oracle_missing=" + str(attempt))
    shell = text_file(image_root, "current-qemu-p1/shell/qemu-serial.log")
    for marker in ("MINIC_LINUX_RUNTIME_SH_READY", "RDINIT_SH_OK",
                   "DONE_RDINIT", "reboot: Power down"):
        if marker not in shell:
            fail("P1_real_shell_oracle_missing")

    return (
        "RV32I_P2_CURRENT_IMAGE=PASS producer_run=" + contract.producer_run +
        " commit=" + contract.producer_sha + " Image_SHA=" + contract.image_sha256 +
        " config_SHA=" + contract.config_sha256 +
        " MiniC_C=1741/1741 original_GNU_Kbuild=true ELF32=true" +
        " P0=PASS shell=PASS P1=12/12 original_C_owners=1741" +
        " header_replayed=" + str(contract.header_replays) +
        " mixed_epochs=false kernel_relink=false"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image-source", type=Path, required=True)
    parser.add_argument("--gnu-source", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    receipt = verify(args.image_source, args.gnu_source)
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(receipt + "\n", encoding="utf-8")
    print(receipt)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, UnicodeError) as exc:
        raise SystemExit(str(exc)) from exc
