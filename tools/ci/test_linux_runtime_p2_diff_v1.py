#!/usr/bin/env python3
"""Synthetic oracle tests; do not count these as Linux guest executions."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tests.external.linux.test_runtime_v2_log_check import P0_P1, P2, FULL_END


SCRIPT = Path(__file__).with_name("linux-runtime-p2-diff-v1.py")
spec = importlib.util.spec_from_file_location("linux_p2_diff", SCRIPT)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

FULL_LOG = P0_P1 + P2 + [FULL_END, "[    1.1] reboot: Power down"]


class PairedP2EvidenceTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.directory = Path(temp.name)
        self.gnu_log = self.directory / "gnu.log"
        self.minic_log = self.directory / "minic.log"
        self.gnu_image = self.directory / "gnu.Image"
        self.minic_image = self.directory / "minic.Image"
        self.initrd = self.directory / "common.cpio.gz"
        self.json_out = self.directory / "result.json"
        for path, payload in ((self.gnu_image, b"gnu-image"),
                              (self.minic_image, b"minic-image"),
                              (self.initrd, b"identical guest fixture")):
            path.write_bytes(payload)

    def invoke(self, *, gnu=None, minic=None, gnu_rc="0", minic_rc="0"):
        self.gnu_log.write_text("\n".join(FULL_LOG if gnu is None else gnu) + "\n")
        self.minic_log.write_text("\n".join(FULL_LOG if minic is None else minic) + "\n")
        return subprocess.run([
            sys.executable, str(SCRIPT), "--target", "rv64",
            "--gnu-log", str(self.gnu_log),
            "--minic-log", str(self.minic_log),
            "--gnu-image", str(self.gnu_image),
            "--minic-image", str(self.minic_image),
            "--initrd", str(self.initrd),
            "--gnu-rc", gnu_rc, "--minic-rc", minic_rc,
            "--json-out", str(self.json_out),
        ], capture_output=True, text=True, check=False)

    def read_report(self):
        return json.loads(self.json_out.read_text())

    def test_two_exactly_full_logs_are_only_exit_zero_case(self):
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        report = self.read_report()
        self.assertEqual(report["comparison"]["verdict"], "BOTH_FULL_P2_PASS")
        self.assertEqual(report["gnu"]["p2_counts"], {"PASS": 18})
        self.assertEqual(report["minic"]["p2_counts"], {"PASS": 18})
        self.assertEqual(report["fixture_provenance"]["gnu_image_sha256"],
                         module.sha256_file(self.gnu_image))

    def test_gnu_pass_minic_fail_is_differential_candidate_not_pass(self):
        bad = [line.replace("FUNCTION_PASS bridge-links-up",
                            "FUNCTION_FAIL bridge-links-up errno=5")
               for line in FULL_LOG]
        actual = self.invoke(minic=bad)
        self.assertEqual(actual.returncode, 1, actual.stderr)
        report = self.read_report()["comparison"]
        self.assertEqual(report["verdict"], "MINIC_REGRESSION_CANDIDATE")
        self.assertEqual(report["gnu_pass_minic_not_pass"], ["bridge"])

    def test_partial_shared_skip_is_environment_gap_not_success(self):
        changed = [line.replace("FUNCTION_PASS overlay-copy-up",
                                "FUNCTION_SKIP overlay-copy-up errno=19")
                   for line in FULL_LOG]
        actual = self.invoke(gnu=changed, minic=changed)
        self.assertEqual(actual.returncode, 1, actual.stderr)
        report = self.read_report()["comparison"]
        self.assertEqual(report["verdict"], "COMMON_ENVIRONMENT_GAP")
        self.assertEqual(report["shared_skip_or_unavailable"], ["overlay-copy-up"])
        self.assertFalse(self.read_report()["gnu"]["strict_p2_pass"])

    def test_incomplete_gnu_baseline_never_proves_compiler_bug(self):
        gnu = [line.replace("FUNCTION_PASS bridge-links-up",
                            "FUNCTION_SKIP bridge-links-up errno=19")
               for line in FULL_LOG]
        minic = [line.replace("FUNCTION_PASS bridge-links-up",
                              "FUNCTION_FAIL bridge-links-up errno=5")
                 for line in FULL_LOG]
        actual = self.invoke(gnu=gnu, minic=minic)
        self.assertEqual(actual.returncode, 1)
        self.assertEqual(self.read_report()["comparison"]["verdict"],
                         "GNU_BASELINE_NOT_QUALIFIED")

    def test_prefixed_spoof_does_not_count_as_bridge(self):
        changed = [line.replace("FUNCTION_PASS bridge-links-up",
                                "FUNCTION_PASS bridge-links-up-fake")
                   for line in FULL_LOG]
        actual = self.invoke(gnu=changed, minic=changed)
        self.assertEqual(actual.returncode, 1)
        report = self.read_report()
        self.assertEqual(report["gnu"]["p2_checks"]["bridge"]["status"], "MISSING")

    def test_custom_rootfs_alias_skip_does_not_turn_into_pass(self):
        lines = ["FUNCTION_SKIP ext4-block-persist errno=19"]
        result = module.collect_check(lines, "rootfs-rw", "RUNTIME_V2_ROOTFS_RW=PASS")
        self.assertEqual(result["status"], "SKIP")
        lines.extend(["RUNTIME_V2_ROOTFS_RW=PASS"])
        result = module.collect_check(lines, "rootfs-rw", "RUNTIME_V2_ROOTFS_RW=PASS")
        self.assertEqual(result["status"], "CONFLICT")

    def test_qemu_nonzero_status_rejects_even_full_markers(self):
        actual = self.invoke(minic_rc="124")
        self.assertEqual(actual.returncode, 1)
        self.assertFalse(self.read_report()["minic"]["strict_p2_pass"])
        self.assertEqual(self.read_report()["comparison"]["verdict"],
                         "MINIC_RUNTIME_FAILURE_CANDIDATE")

    def test_same_skips_with_kernel_oops_are_not_called_environment_only(self):
        changed = [line.replace("FUNCTION_PASS overlay-copy-up",
                                "FUNCTION_SKIP overlay-copy-up errno=19")
                   for line in FULL_LOG]
        gnu = changed
        minic = changed + ["[    1.2] Kernel panic - not syncing: deliberate"]
        actual = self.invoke(gnu=gnu, minic=minic)
        self.assertEqual(actual.returncode, 1)
        self.assertEqual(self.read_report()["comparison"]["verdict"],
                         "DIFFERENTIAL_RUNTIME_FAILURE_GNU_BASELINE_INCOMPLETE")

    def test_qemu_wrapper_size_mismatch_is_not_a_compiler_regression(self):
        gnu = FULL_LOG + [
            "QEMU_EXPLICIT_INITRD=PASS image_bytes=9 initrd_bytes=23 start=0x1"
        ]
        minic = FULL_LOG + [
            "QEMU_EXPLICIT_INITRD=PASS image_bytes=999 initrd_bytes=23 start=0x2"
        ]
        actual = self.invoke(gnu=gnu, minic=minic)
        self.assertEqual(actual.returncode, 1)
        report = self.read_report()
        self.assertTrue(report["gnu"]["qemu_explicit_initrd_logged_sizes_match"])
        self.assertFalse(report["minic"]["qemu_explicit_initrd_logged_sizes_match"])
        self.assertEqual(report["comparison"]["verdict"],
                         "QEMU_ARTIFACT_IDENTITY_MISMATCH")

    def test_missing_input_is_error_not_a_pass(self):
        self.gnu_image.unlink()
        actual = self.invoke()
        self.assertEqual(actual.returncode, 2)
        self.assertIn("INPUT_ERROR", actual.stderr)


if __name__ == "__main__":
    unittest.main()
