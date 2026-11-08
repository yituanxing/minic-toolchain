#!/usr/bin/env python3
"""AArch64/LoongArch64 full-C BusyBox: isolated negative and wiring contracts.

This is a fast unit/static gate, not a substitute for original Kbuild/QEMU.
"""
import importlib.util
from pathlib import Path
import sys
import unittest


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
spec = importlib.util.spec_from_file_location("mixed_bulk_kbuild", HERE / "mixed_bulk_kbuild.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FullCBusyBoxContract(unittest.TestCase):
    def setUp(self):
        self.oracle = {f"fixture-{i}": (0, f"out-{i}".encode(), b"")
                       for i in range(38)}
        self.kw = dict(target="aarch64", owners=581, minic_owners=581,
                       fallbacks=0, verified=581, link_rc=0,
                       original_sha="original-elf-sha", mixed_sha="minic-elf-sha",
                       qemu="qemu-aarch64", oracle=self.oracle,
                       mixed=self.oracle.copy())

    def test_exact_full_c_gnu_link_qemu_contract(self):
        for target in ("aarch64", "loongarch64"):
            with self.subTest(target=target):
                self.assertTrue(module.full_c_binary_accepted(
                    **{**self.kw, "target": target}))

    def test_missing_owner_or_gnu_fallback_fails_closed(self):
        for target in ("aarch64", "loongarch64"):
            for changes in ({"owners": 580}, {"minic_owners": 580},
                            {"fallbacks": 1}, {"verified": 580}):
                with self.subTest(target=target, changes=changes):
                    self.assertFalse(module.full_c_binary_accepted(
                        **{**self.kw, "target": target, **changes}))

    def test_link_or_non_aarch64_or_unproven_elf_fails_closed(self):
        for target in ("aarch64", "loongarch64"):
            for changes in ({"link_rc": 1}, {"target": "x86_64"},
                            {"original_sha": "minic-elf-sha"},
                            {"mixed_sha": ""}, {"qemu": ""}):
                with self.subTest(target=target, changes=changes):
                    self.assertFalse(module.full_c_binary_accepted(
                        **{**self.kw, "target": target, **changes}))

    def test_qemu_missing_fixture_failure_and_byte_drift_fail_closed(self):
        reduced = dict(self.oracle)
        reduced.pop("fixture-37")
        timeout = dict(self.oracle)
        timeout["fixture-0"] = (124, b"", b"timeout")
        different = dict(self.oracle)
        different["fixture-0"] = (0, b"wrong", b"")
        for target in ("aarch64", "loongarch64"):
            for changes in ({"oracle": reduced, "mixed": reduced},
                            {"mixed": reduced}, {"mixed": timeout},
                            {"mixed": different}):
                with self.subTest(target=target, changes=list(changes)):
                    self.assertFalse(module.full_c_binary_accepted(
                        **{**self.kw, "target": target, **changes}))

    def test_production_wiring_is_explicit_and_fail_closed(self):
        outer = (HERE / "probe-four-backend-all-kbuild.sh").read_text()
        wrapper = (HERE / "probe-aarch64-full-c-busybox.sh").read_text()
        loong_wrapper = (HERE / "probe-loongarch64-full-c-busybox.sh").read_text()
        workflow = (ROOT / ".github/workflows/minic-aarch64-busybox-581-stage5-strict.yml").read_text()
        loong_workflow = (ROOT / ".github/workflows/minic-loongarch64-busybox-581-stage5-strict.yml").read_text()
        bulk = (HERE / "mixed_bulk_kbuild.py").read_text()
        self.assertIn("FOUR_BACKEND_BUSYBOX_FULL_C_BINARY=1", wrapper)
        self.assertIn("FOUR_BACKEND_BUSYBOX_FULL_C_BINARY=1", loong_wrapper)
        self.assertIn("BUSYBOX_ALLYES_FULL_C_BUSYBOX=", outer)
        self.assertIn("stage=missing-real-linked-QEMU-proof", outer)
        self.assertIn("probe-aarch64-full-c-busybox.sh", workflow)
        self.assertIn("probe-loongarch64-full-c-busybox.sh", loong_workflow)
        self.assertIn("grep -Fxq 'BUSYBOX_FULL_C_BINARY=PASS", workflow)
        self.assertIn("grep -Fxq 'BUSYBOX_FULL_C_BINARY=PASS", loong_workflow)
        self.assertIn("grep -Fxq 'FOUR_BACKEND_BUSYBOX_FULL_C_BINARY=PASS", workflow)
        self.assertIn("grep -Fxq 'FOUR_BACKEND_BUSYBOX_FULL_C_BINARY=PASS", loong_workflow)
        self.assertIn("full_c_binary_accepted(", bulk)
        self.assertLess(bulk.index("if failures:"),
                        bulk.index("if full_c_opt_in == \"1\":", bulk.index("if failures:")))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(FullCBusyBoxContract)
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    if not result.wasSuccessful():
        raise SystemExit(1)
    print(f"CROSS_FULL_C_BUSYBOX_CONTRACT=PASS tests={result.testsRun} "
          "negative_cases=26 targets=aarch64,loongarch64 "
          "static_wiring=YES not_runtime=YES")
