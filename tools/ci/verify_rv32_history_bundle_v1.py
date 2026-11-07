#!/usr/bin/env python3
"""Strictly qualify an archived *historical* RV32 Linux 1741-owner run.

This is preservation only: it does not compile the current MiniC or certify
that a new compiler SHA passes Linux. Exits nonzero on missing/partial evidence.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

RUN_ID = 41212514498
HISTORICAL_MERGE_SHA = "796f2b44fada71218a22fc506ae50ecfe6b43342"
IMAGE_SHA = "a762cb8ba55a3cbb52a1f85e61d40524941c8226a33cb35daf2bff1acd75a5a2"
CONFIG_SHA = "4c6c19f79741a8a31ec52ad57b1a6bc56b81dd6996cbf5e1bdc63f587d85244c"
IMAGE_ARTIFACT = f"linux-rv32i-seven-shard-Image-{HISTORICAL_MERGE_SHA}"
GNU_ARTIFACT = f"linux-rv32i-original-kbuild-{HISTORICAL_MERGE_SHA}"


def require(ok: bool, detail: str) -> None:
    if not ok:
        raise ValueError(f"RV32_HISTORY_PRESERVE=FAIL {detail}")


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def owners(path: Path) -> list[str]:
    require(path.is_file(), f"missing={path}")
    names = [s.strip() for s in path.read_text().splitlines()
             if s.strip() and not s.lstrip().startswith("#")]
    require(len(names) == len(set(names)), f"duplicate_owners={path}")
    for name in names:
        parts = name.split("/")
        require(len(parts) >= 2 and all(p not in ("", ".", "..") for p in parts)
                and name.endswith(".o") and "\\" not in name,
                f"invalid_owner={name}")
    return names


def attest_elf(path: Path, owner: str) -> None:
    with path.open("rb") as f:
        header = f.read(52)
    require(len(header) >= 52 and header[:6] == b"\x7fELF\x01\x01"
            and struct.unpack_from("<H", header, 16)[0] == 1
            and struct.unpack_from("<H", header, 18)[0] == 243,
            f"not_elf32_le_riscv_rel={owner}")
    flags = struct.unpack_from("<I", header, 36)[0]
    # The strict original source contract forbids RVC and hard-float ABI.
    require((flags & (0x01 | 0x06)) == 0,
            f"rv32i_ilp32_elf_flags={owner}:0x{flags:x}")


def validate(root: Path) -> dict:
    names = {f"shard-{i}" for i in range(7)} | {IMAGE_ARTIFACT, GNU_ARTIFACT}
    actual = {p.name for p in root.iterdir() if p.is_dir()}
    require(actual == names, f"expected_9_artifacts={sorted(names)} got={sorted(actual)}")

    image_root = root / IMAGE_ARTIFACT
    gnu_root = root / GNU_ARTIFACT
    image = image_root / "out/arch/riscv/boot/Image"
    require(image.is_file() and image.stat().st_size == 185681328,
            "historical_Image_missing_or_size_mismatch")
    require(digest(image) == IMAGE_SHA, "historical_Image_sha_mismatch")
    cfg = gnu_root / "out/.config"
    require(cfg.is_file() and digest(cfg) == CONFIG_SHA,
            "GNU_Kconfig_sha_mismatch")
    baseline = (gnu_root / "provenance/gcc-baseline.txt").read_text()
    for exact in ("schema=linux-gnu-exact-config-v1", "linux_release=6.6.143",
                  "elf_class=ELF32", "isa=rv32i", "abi=ilp32",
                  f"config_sha256={CONFIG_SHA}", "c_owner_count=1741"):
        require(exact in baseline.splitlines(), f"GNU_baseline_missing={exact}")
    original = owners(gnu_root / "provenance/full-c-objects.txt")
    require(len(original) == 1741, f"original_owners={len(original)}")

    union: list[str] = []
    for shard in range(7):
        base = root / f"shard-{shard}"
        subset = owners(base / "full-objects.txt")
        expected = original[shard::7]
        require(subset == expected,
                f"historical_split_mismatch_shard={shard}")
        manifest = (base / "minic-manifest.sha256").read_text().splitlines()
        require(len(manifest) == len(expected),
                f"manifest_count_shard={shard} actual={len(manifest)}")
        ident = (base / "minic-identity.txt").read_text().strip()
        require(re.fullmatch(r"[a-f0-9]{64}", ident) is not None,
                f"invalid_compiler_abi_identity_shard={shard}")
        require((base / "cohort.log").is_file()
                and f"COHORT_COMPILE=PASS objects={len(expected)}"
                in (base / "cohort.log").read_text(),
                f"missing_original_compile_receipt_shard={shard}")
        for owner, line in zip(expected, manifest):
            match = re.fullmatch(r"([a-f0-9]{64})  (minic/.+\.o)", line)
            require(match is not None and match[2] == f"minic/{owner}",
                    f"manifest_owner_mismatch_shard={shard} owner={owner}")
            output = base / match[2]
            require(output.is_file() and digest(output) == match[1],
                    f"MiniC_object_sha_mismatch_shard={shard} owner={owner}")
            attest_elf(output, owner)
        union.extend(subset)
    require(len(union) == 1741 and set(union) == set(original),
            "historical_MiniC_1741_owner_union_failed")

    boots = []
    for i in range(1, 13):
        trial = image_root / f"current-qemu-p1/trials/{i}"
        lines = (trial / "provenance.txt").read_text().splitlines()
        require(lines == ["qemu_status=0", f"image_sha256={IMAGE_SHA}"],
                f"qemu_exit_or_Image_mismatch_boot={i}")
        serial = (trial / "qemu-serial.log").read_text(errors="replace")
        for token in ("RUNTIME_V2_SYSCALL_END pass=12 fail=0",
                      "RV32I_LINUX_P1=PASS syscalls=12",
                      "INIT_RUNTIME_PROBE=PASS", "USER_SHELL_OK",
                      "MINIC_LINUX_RUNTIME_INIT_END fail=0", "reboot: Power down"):
            require(token in serial, f"original_QEMU_boot={i}_missing={token}")
        require(serial.count("RUNTIME_V2_SYSCALL_PASS ") == 12,
                f"historical_QEMU_syscall_count_boot={i}")
        boots.append({"boot": i, "image_sha256": IMAGE_SHA, "qemu_exit": 0,
                      "syscalls_pass": 12})

    return {"verdict": "RV32_HISTORY_PRESERVE=PASS", "scope": "HISTORICAL_ONLY",
            "minic_current_head": "NOT_RUN", "source_run": RUN_ID,
            "source_merge_commit": HISTORICAL_MERGE_SHA,
            "source_isa": "rv32i", "source_abi": "ilp32",
            "historical_image_sha256": IMAGE_SHA, "gnu_config_sha256": CONFIG_SHA,
            "historical_minic_object_count": 1741,
            "independent_QEMU_P1_boots": boots,
            "candidate_current_head_Linux_P1": "NOT_RUN"}


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--input", required=True, type=Path)
    p.add_argument("--receipt", type=Path)
    a = p.parse_args()
    result = validate(a.input)
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if a.receipt:
        a.receipt.parent.mkdir(parents=True, exist_ok=True)
        a.receipt.write_text(encoded)
    print("RV32_HISTORY_PRESERVE=PASS source_run=41212514498 "
          "original_MiniC_C_GNU_AS=1741/1741 QEMU_P1=12/12 "
          "retention_days=90 compiler_current_head=NOT_RUN")


if __name__ == "__main__":
    main()
