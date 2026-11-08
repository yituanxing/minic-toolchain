#!/usr/bin/env python3
"""Fast local fail-closed fixture tests for RV32 current-head P2 admission."""
from __future__ import annotations

from dataclasses import replace
import hashlib
import importlib.util
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest


MODULE_PATH = Path(__file__).with_name("linux-rv32i-p2-current-image-v1.py")
SPEC = importlib.util.spec_from_file_location("rv32_p2_current_image", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
audit = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = audit
SPEC.loader.exec_module(audit)


def sha(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def put(root: Path, relative: str, value: str | bytes) -> None:
    target = root / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(value.encode("utf-8") if isinstance(value, str) else value)


class CurrentImageAdmissionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root = Path(self.tmp.name)
        self.image_root = root / "image"
        self.gnu_root = root / "gnu"
        binary = b"synthetic immutable RV32 Image"
        config = b"original Kconfig synthetic SHA"
        self.contract = replace(audit.PINNED, image_sha256=sha(binary),
                                config_sha256=sha(config))
        c = self.contract
        put(self.image_root, "out/arch/riscv/boot/Image", binary)
        put(self.gnu_root, "out/.config", config)
        put(self.gnu_root, "provenance/gcc-baseline.txt", "".join(
            f"{key}={value}\n" for key, value in {
                "schema": "linux-gnu-exact-config-v1",
                "linux_release": "6.6.143",
                "linux_archive_sha256": c.linux_source_sha256,
                "elf_class": "ELF32", "isa": "rv32i", "abi": "ilp32",
                "config_sha256": c.config_sha256,
                "c_owner_count": str(c.owners),
            }.items()))
        self.owners = [f"drivers/test/owner{i:04}.o" for i in range(c.owners)]
        put(self.gnu_root, "provenance/full-c-objects.txt",
            "\n".join(self.owners) + "\n")
        put(self.image_root, "merged/minic-manifest.sha256",
            "".join("a" * 64 + "  minic/" + name + "\n" for name in self.owners))
        put(self.image_root, "merged/minic-identity.txt", "f" * 64 + "\n")
        put(self.image_root, "merged/minic-vmlinux-elf.txt",
            "Class: ELF32\nMachine: RISC-V\n")
        put(self.image_root, "current-qemu-p0/qemu-serial.log",
            "RV32I_LINUX_PID1=PASS syscall=getpid,write elf=ELF32 abi=ilp32\n")
        put(self.image_root, "merged/union-result.txt",
            "RV32I_UNION_LINK=PASS ELF32=1 full_MiniC_C=true " +
            "original_GNU_Kbuild=true image_sha256=" + c.image_sha256 +
            " QEMU_P1=NOT_RUN\n")
        put(self.image_root, "merged/header-affected-objects.txt",
            "\n".join(self.owners[-c.header_replays:]) + "\n")
        put(self.image_root, "merged/header-replay-hashes.sha256",
            "".join("b" * 64 + "  " + name + "\n"
                    for name in self.owners[-c.header_replays:]))
        put(self.image_root, "current-qemu-p1/source-identity.txt",
            f"commit={c.producer_sha}\nimage_sha256={c.image_sha256}\n"
            f"producer_run={c.producer_run}\n")
        put(self.image_root, "current-qemu-p1/p1-repeats.tsv",
            "attempt\timage_sha256\tverdict\n" +
            "".join(f"{n}\t{c.image_sha256}\tPASS\n"
                    for n in range(1, c.p1_boots + 1)))
        put(self.image_root, "current-qemu-p1/shell/qemu-serial.log",
            "MINIC_LINUX_RUNTIME_SH_READY\nRDINIT_SH_OK\n"
            "DONE_RDINIT\nreboot: Power down\n")
        for n in range(1, c.p1_boots + 1):
            prefix = f"current-qemu-p1/trials/{n}"
            put(self.image_root, prefix + "/provenance.txt",
                f"qemu_status=0\nimage_sha256={c.image_sha256}\n")
            put(self.image_root, prefix + "/qemu-serial.log",
                "RUNTIME_V2_SYSCALL_END pass=12 fail=0\n"
                "RV32I_LINUX_P1=PASS syscalls=12 child=ELF32 "
                "ilp32=true execve=true\nreboot: Power down\n")

    def verify(self) -> str:
        return audit.verify(self.image_root, self.gnu_root, self.contract)

    def test_exact_current_source_and_twelve_p1_receipts(self) -> None:
        self.assertIn("RV32I_P2_CURRENT_IMAGE=PASS", self.verify())
        self.assertIn("mixed_epochs=false", self.verify())

    def test_reject_old_or_different_image(self) -> None:
        put(self.image_root, "out/arch/riscv/boot/Image", b"old Image")
        with self.assertRaisesRegex(ValueError, "immutable_Image_SHA_mismatch"):
            self.verify()

    def test_reject_wrong_config_epoch(self) -> None:
        put(self.gnu_root, "out/.config", b"changed config")
        with self.assertRaisesRegex(ValueError, "original_GNU_Kconfig_SHA_mismatch"):
            self.verify()

    def test_reject_old_mixed_epoch_source(self) -> None:
        put(self.image_root, "merged/union-result.txt",
            "RV32I_FOUR_OWNER_REPLAY_LINK=PASS mixed_epochs=true\n")
        with self.assertRaisesRegex(ValueError, "not_full_current_MiniC_GNU_link"):
            self.verify()

    def test_reject_foreign_producer(self) -> None:
        put(self.image_root, "current-qemu-p1/source-identity.txt",
            "commit=796f2b44fada71218a22fc506ae50ecfe6b43342\n"
            f"image_sha256={self.contract.image_sha256}\nproducer_run=41212514498\n")
        with self.assertRaisesRegex(ValueError, "foreign_or_mixed_P1_producer"):
            self.verify()

    def test_reject_missing_original_owner(self) -> None:
        put(self.gnu_root, "provenance/full-c-objects.txt",
            "\n".join(self.owners[:-1]) + "\n")
        with self.assertRaisesRegex(ValueError, "wrong_owner_count"):
            self.verify()

    def test_reject_unreplayed_generated_header_owner(self) -> None:
        put(self.image_root, "merged/header-replay-hashes.sha256",
            "b" * 64 + "  " + self.owners[-1] + "\n")
        with self.assertRaisesRegex(ValueError, "unproven_generated_header_replay"):
            self.verify()

    def test_reject_one_unproven_p1_boot(self) -> None:
        put(self.image_root, "current-qemu-p1/trials/8/provenance.txt",
            f"qemu_status=1\nimage_sha256={self.contract.image_sha256}\n")
        with self.assertRaisesRegex(ValueError, "unproven_P1_QEMU_trial=8"):
            self.verify()

    def test_reject_missing_real_qemu_p0(self) -> None:
        put(self.image_root, "current-qemu-p0/qemu-serial.log",
            "untrusted host oracle\n")
        with self.assertRaisesRegex(ValueError, "current_P0_real_QEMU_PID1_oracle_missing"):
            self.verify()

    def test_reject_one_missing_syscall_oracle(self) -> None:
        put(self.image_root, "current-qemu-p1/trials/9/qemu-serial.log",
            "RV32I_LINUX_P1=PASS syscalls=12\nreboot: Power down\n")
        with self.assertRaisesRegex(ValueError, "P1_serial_oracle_missing=9"):
            self.verify()

    def test_workflow_never_restores_legacy_or_relinks_kernel(self) -> None:
        workflow = (MODULE_PATH.parents[2] /
                    ".github/workflows/minic-rv32i-linux-p2-strict-frontier.yml").read_text()
        for banned in ("41212514498", "796f2b44fada71218a22fc506ae50ecfe6b43342",
                       "a762cb8ba55a3cbb52a1f85e61d40524941c8226a33cb35daf2bff1acd75a5a2",
                       "linux-rv32i-p2-refresh-virtio-v1.sh",
                       "build/rv32i-linux-entry/out/arch/riscv/boot/Image",
                       "1738 frozen", "Restore seven original MiniC owner shards"):
            self.assertNotIn(banned, workflow)
        self.assertEqual(workflow.count("run-id: '42442670106'"), 2)
        self.assertIn("test \"$(find \"$modules\" -name '*.ko' -type f | wc -l)\" -eq 38",
                      workflow)
        self.assertIn("RV32I_QEMU_P2_FULL18=PASS", workflow)


if __name__ == "__main__":
    unittest.main()
