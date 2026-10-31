#!/usr/bin/env python3
"""Synthetic fail-closed oracle tests; never interpreted as a real QEMU PASS."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest


CI = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "loongarch_p2_17", CI / "linux-loongarch64-p2-gnu-17-check-v1.py")
assert spec is not None and spec.loader is not None
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


class StrictLoongArch17(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="loong-p2-17-oracle-")
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name) / "linux-6.6.143"
        (self.source / "arch/loongarch/kernel").mkdir(parents=True)
        (self.source / "Makefile").write_text(
            "VERSION = 6\nPATCHLEVEL = 6\nSUBLEVEL = 143\n")
        self.config = "\n".join(f"CONFIG_{s}=y" for s in oracle.BUILTINS) + "\n"
        lines = []
        for group, identifier, pattern, _ in oracle.canonical.load_checks(
                oracle.ROOT / "tests/external/linux/runtime_v2_checks.tsv", "p2"):
            if group == "p2" and identifier == "kvm":
                continue
            if identifier == "linux-version":
                lines.append("Linux version 6.6.143")
            elif identifier == "pid1":
                lines.append("Run /init as init process")
            elif identifier == "powerdown":
                lines.append("[    8.000000] reboot: Power down")
            else:
                lines.append(pattern)
        lines.extend(("FUNCTION_PASS ext4-block-persist",
                      "P2_CONNTRACK_TRACE before=0 after=1 attempts=1",
                      "RUNTIME_V2_P2_BEGIN", oracle.EXPECTED_BLOCK,
                      oracle.EXPECTED_END, "INIT_P2_PROBE=STRUCTURAL_BLOCKED"))
        self.log = "\n".join(lines) + "\n"

    def reasons(self, *, log: str | None = None, rc: int = 0,
                config: str | None = None) -> list[str]:
        return oracle.check(self.log if log is None else log, rc,
                            self.config if config is None else config,
                            self.source)

    def test_exact_17_with_structural_block_is_accepted(self) -> None:
        self.assertEqual([], self.reasons())

    def test_actual_qemu_failure_is_rejected(self) -> None:
        self.assertIn("qemu-rc=124", self.reasons(rc=124))

    def test_any_skip_is_rejected(self) -> None:
        self.assertTrue(self.reasons(log=self.log + "FUNCTION_SKIP kvm-device-created\n"))

    def test_fabricated_kvm_pass_is_rejected(self) -> None:
        self.assertTrue(self.reasons(log=self.log + "FUNCTION_PASS kvm-device-created\n"))

    def test_full18_endpoint_is_rejected(self) -> None:
        self.assertTrue(self.reasons(
            log=self.log + "RUNTIME_V2_P2_FULL_END pass=18 fail=0 skip=0\n"))

    def test_missing_real_check_is_rejected(self) -> None:
        self.assertTrue(self.reasons(
            log=self.log.replace("FUNCTION_PASS bridge-links-up\n", "")))

    def test_unproven_conntrack_flow_is_rejected(self) -> None:
        self.assertTrue(self.reasons(log=self.log.replace(
            "P2_CONNTRACK_TRACE before=0 after=1 attempts=1\n", "")))

    def test_duplicate_real_check_is_rejected(self) -> None:
        self.assertTrue(self.reasons(
            log=self.log + "FUNCTION_PASS conntrack-entry-created\n"))

    def test_missing_kvm_structural_negative_is_rejected(self) -> None:
        self.assertTrue(self.reasons(log=self.log.replace(oracle.EXPECTED_BLOCK, "")))

    def test_enabled_kvm_config_is_rejected(self) -> None:
        self.assertTrue(self.reasons(config=self.config + "CONFIG_KVM=y\n"))

    def test_kernel_with_kvm_tree_is_rejected(self) -> None:
        (self.source / "arch/loongarch/kvm").mkdir()
        self.assertTrue(self.reasons())

    def test_kernel_oops_is_rejected(self) -> None:
        self.assertTrue(self.reasons(log=self.log + "[9] BUG: genuine crash\n"))


class ExactCanonicalSourceCalls(unittest.TestCase):
    def setUp(self) -> None:
        self.original = oracle.CANONICAL_PROBE.read_text()
        self.loong = oracle.LOONG_PROBE.read_text()

    def test_all_seventeen_calls_and_kat_bytes_match_canonical(self) -> None:
        self.assertTrue(oracle.source_call_agreement(self.original, self.loong))

    def test_omitted_sha512_suffix_cannot_reach_expensive_qemu(self) -> None:
        bad = self.loong.replace("e2a9ac94fa54ca49f", "", 1)
        self.assertFalse(oracle.source_call_agreement(self.original, bad))

    def test_omitted_sha3_hex_nibble_cannot_reach_expensive_qemu(self) -> None:
        bad = self.loong.replace("5c172d6bd", "5c1726bd", 1)
        self.assertFalse(oracle.source_call_agreement(self.original, bad))

    def test_unexpected_kvm_operation_is_rejected(self) -> None:
        bad = self.loong.replace("    virtio_gpu_bound();",
                                 "    kvm_api_device();\n    virtio_gpu_bound();", 1)
        self.assertFalse(oracle.source_call_agreement(self.original, bad))


if __name__ == "__main__":
    unittest.main()
