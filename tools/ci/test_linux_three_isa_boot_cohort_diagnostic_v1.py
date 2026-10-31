#!/usr/bin/env python3
"""Small, no-Linux smoke tests for diagnostic-only x86 cohort object swapping."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import types
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).with_name("linux-three-isa-boot-cohort-diagnostic-v1.py")
SPEC = importlib.util.spec_from_file_location("linux_boot_cohort_diagnostic", SCRIPT)
assert SPEC and SPEC.loader
diag = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(diag)


def put(path: Path, data: bytes) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return hashlib.sha256(data).hexdigest()


def fixture_rows() -> list[dict]:
    names = [
        "arch/x86/boot/compressed/misc.o",
        "arch/x86/boot/compressed/kaslr.o",
        "kernel/sched/core.o",
        "init/main.o",
    ]
    return [{"index": i, "shard": i % 7, "object": name,
             "sha256": hashlib.sha256(name.encode()).hexdigest()}
            for i, name in enumerate(names)]


class DiagnosticFixture(unittest.TestCase):
    def setUp(self):
        self.hold = tempfile.TemporaryDirectory(prefix="minic-cohort-test-")
        self.addCleanup(self.hold.cleanup)
        self.root = Path(self.hold.name)
        self.work = self.root / "gnu-work"
        self.out = self.work / "build"
        self.out.mkdir(parents=True)
        self.rows = fixture_rows()
        self.original = {}
        self.minic_sha = {}
        for row in self.rows:
            name = row["object"]
            self.original[name] = put(self.out / name, b"gnu:" + name.encode())
            self.minic_sha[name] = put(self.root / "staged" / name,
                                       b"minic:" + name.encode())
        put(self.out / ".config", b"CONFIG_X86_64=y\n")
        put(self.out / "arch/x86/boot/bzImage", b"genuine GNU Image")
        self.snapshot = self.root / "snap" / "pristine-gnu-build"
        diag.copy_independent_tree(self.out, self.snapshot)
        self.gnu_receipt = self.root / "boot/gnu-qemu-receipt.json"
        put(self.gnu_receipt, b"gnu-receipt")
        self.gnu_log = self.root / "boot/p0.log"
        put(self.gnu_log, b"GNU_PASS=1\n")
        self.initramfs = self.root / "initramfs.cpio.gz"
        put(self.initramfs, b"initramfs")
        self.manifest = self.work / "frozen/manifest.json"
        put(self.manifest, json.dumps({"rows": self.rows}).encode())
        self.results = self.root / "all-c-owners-result.json"
        put(self.results, json.dumps([
            {"object": row["object"], "status": "MINIC_S_GNU_AS_PASS",
             "input_sha256": row["sha256"], "obj_sha256": self.minic_sha[row["object"]]}
            for row in self.rows]).encode())
        self.minic = self.root / "minic"
        put(self.minic, b"test fake executable")
        self.compiler = {"compiler_sha256": "3" * 64,
                         "compiler_driver_sha256": "1" * 64,
                         "compiler_cc_sha256": "2" * 64}
        info = {"schema": diag.SCHEMA, "target": diag.TARGET,
                "phase": "prepared-pristine-gnu", "diagnostic_only": True,
                "gnu_work": str(self.work), "gnu_receipt": str(self.gnu_receipt),
                "gnu_receipt_sha256": diag.digest(self.gnu_receipt),
                "gnu_p0_log": str(self.gnu_log),
                "gnu_p0_log_sha256": diag.digest(self.gnu_log),
                "initramfs": str(self.initramfs),
                "initramfs_sha256": diag.digest(self.initramfs),
                "original_manifest_sha256": diag.digest(self.manifest),
                "original_owner_count": len(self.rows),
                "compressed_owners": [row["object"] for row in self.rows[:2]],
                "config_sha256": diag.digest(self.out / ".config"),
                "gnu_image_sha256": diag.digest(self.out / "arch/x86/boot/bzImage"),
                "original_gnu_c_owner_sha256": self.original,
                **self.compiler}
        put(self.root / "snap/prepare.json", json.dumps(info).encode())

    def test_staged_seven_manifest_shas_and_owner_modes(self):
        with patch.object(diag, "COMPRESSED_COUNT", 2):
            self.assertEqual(diag.select_owners(self.rows, "gnu-plus-compressed"),
                             [row["object"] for row in self.rows[:2]])
            self.assertEqual(diag.select_owners(self.rows, "minic-except-compressed"),
                             [row["object"] for row in self.rows[2:]])
        result = diag.verify_staged(self.rows, self.root / "staged", self.results,
                                    lambda path, row: None)
        self.assertEqual(result, self.minic_sha)
        put(self.root / "staged/kernel/sched/core.o", b"tampered")
        with self.assertRaisesRegex(diag.DiagnosticError, "SHA changed"):
            diag.verify_staged(self.rows, self.root / "staged", self.results,
                               lambda path, row: None)

    def test_safe_snapshot_has_unique_inodes_and_original_bytes(self):
        for name, expected in self.original.items():
            orig = self.out / name
            saved = self.snapshot / name
            self.assertEqual(diag.digest(saved), expected)
            self.assertNotEqual((orig.stat().st_dev, orig.stat().st_ino),
                                (saved.stat().st_dev, saved.stat().st_ino))
        with self.assertRaisesRegex(diag.DiagnosticError, "existing Kbuild snapshot"):
            diag.copy_independent_tree(self.out, self.snapshot)
        for name in ("/absolute.o", "../../escape.o", "a/not-object.c", "owner.o"):
            with self.assertRaises(diag.DiagnosticError):
                diag.safe_owner(name)

    def test_reconcile_detects_recompiled_gnu_c_and_overwritten_minic(self):
        selected = [row["object"] for row in self.rows[:2]]
        actual = {**self.original, **{k: self.minic_sha[k] for k in selected}}
        actual["init/main.o"] = "a" * 64
        verdict = diag.reconcile_owners(self.rows, self.original, self.minic_sha,
                                        selected, actual, "  CC  init/main.o\n")
        self.assertEqual(verdict["other_rebuilt_c_owners"], ["init/main.o"])
        self.assertEqual(verdict["unlogged_other_owner_changes"], [])
        self.assertFalse(verdict["strict_qualification_allowed"])
        actual[selected[0]] = self.original[selected[0]]
        verdict = diag.reconcile_owners(self.rows, self.original, self.minic_sha,
                                        selected, actual, "")
        self.assertEqual(verdict["selected_overwritten"], [selected[0]])

    def test_probe_uses_gnu_kbuild_and_restores_official_build_on_p0_match(self):
        fake_candidate = types.SimpleNamespace(
            TARGETS={diag.TARGET: ("x86", "", "gcc", "bzImage", "arch/x86/boot/bzImage", 62)},
            compiler_pair=lambda compiler: self.compiler,
            elf_machine=lambda path, machine: None)
        fake_oracle = types.SimpleNamespace(
            executable=lambda command: Path("/usr/bin/qemu-system-x86_64"),
            run_lane=lambda *args: self._fake_qemu_lane(*args),
            CertificationError=type("FakeCertificationError", (Exception,), {}))
        fake_diff = types.SimpleNamespace(compare=lambda *args: {"GNU_PASS": "1"},
                                          RuntimeMismatch=ValueError)
        original_gnu_image = diag.digest(self.out / "arch/x86/boot/bzImage")
        drift_case = {"enabled": False}
        link_fail_case = {"enabled": False}

        def module(filename, name):
            if filename.endswith("candidate-v1.py"):
                return fake_candidate
            if filename.endswith("gnu-qemu-v1.py"):
                return fake_oracle
            if filename.endswith("runtime-diff-v1.py"):
                return fake_diff
            raise AssertionError(filename)

        def fake_make(command, **kwargs):
            self.assertEqual(command[0], "make")
            self.assertIn("ARCH=x86", command)
            self.assertIn("CC=gcc", command)
            self.assertIn("bzImage", command)
            if drift_case["enabled"]:
                # Real x86 Kbuild can regenerate misc.o/kaslr.o after swapping.
                # The synthetic link deliberately overwrites selected MiniC.
                name = self.rows[0]["object"]
                put(self.out / name, (self.snapshot / name).read_bytes())
            put(self.out / "arch/x86/boot/bzImage", b"diagnostic rebuilt image")
            return types.SimpleNamespace(returncode=1 if link_fail_case["enabled"] else 0,
                                         stdout="  LD vmlinux\n")

        args = argparse.Namespace(gnu_work=self.work, snapshot_dir=self.root / "snap",
                                  minic=self.minic, staged=self.root / "staged",
                                  results=self.results, trial_dir=self.root / "trial",
                                  mode="gnu-plus-compressed", link_timeout=900,
                                  qemu_timeout=240, max_live_cc=32)
        original_run = subprocess.run

        def dispatch(command, **kwargs):
            if command[0] == "cp":
                return original_run(command, **kwargs)
            return fake_make(command, **kwargs)

        with (patch.object(diag, "COMPRESSED_COUNT", 2),
              patch.object(diag, "source_rows", return_value=self.rows),
              patch.object(diag, "load_sibling", side_effect=module),
              patch.object(diag, "live_census", side_effect=lambda out, evidence: {
                  name: diag.digest(out / name) for name in self.original}),
              patch.object(diag.subprocess, "run", side_effect=dispatch)):
            # Preserve real `cp` calls, only mock the GNU make invocation.
            rc = diag.probe(args)
            self.assertEqual(rc, 0)
            args.mode = "minic-except-compressed"
            args.trial_dir = self.root / "second-trial"
            rc = diag.probe(args)
            self.assertEqual(rc, 0)
            drift_case["enabled"] = True
            args.mode = "gnu-plus-compressed"
            args.trial_dir = self.root / "drift-trial"
            drift_rc = diag.probe(args)
            drift_case["enabled"] = False
            link_fail_case["enabled"] = True
            args.trial_dir = self.root / "link-fail-trial"
            fail_rc = diag.probe(args)
        self.assertEqual(rc, 0)
        self.assertEqual(drift_rc, 2)
        self.assertEqual(fail_rc, 2)
        record = diag.read_json(self.root / "trial/diagnostic-receipt.json")
        self.assertEqual(record["verdict"], "DIAGNOSTIC_P0_MATCHES_GNU_NOT_ACCEPTANCE")
        self.assertTrue(record["original_official_build_restored"])
        self.assertFalse(record["strict_qualification_allowed"])
        self.assertEqual(record["selected_minic_owner_count"], 2)
        inverse = diag.read_json(self.root / "second-trial/diagnostic-receipt.json")
        self.assertEqual(inverse["verdict"], "DIAGNOSTIC_P0_MATCHES_GNU_NOT_ACCEPTANCE")
        self.assertEqual(inverse["selected_minic_owners"],
                         [row["object"] for row in self.rows[2:]])
        self.assertTrue(inverse["original_official_build_restored"])
        drift = diag.read_json(self.root / "drift-trial/diagnostic-receipt.json")
        self.assertEqual(drift["verdict"], "DIAGNOSTIC_INCONCLUSIVE_OWNER_DRIFT")
        self.assertEqual(drift["p0_observation"], "MATCHES_GNU_SEMANTIC_MARKERS")
        self.assertFalse(drift["intended_cohort_preserved_postlink"])
        self.assertIn(self.rows[0]["object"], drift["selected_actual_original_gnu_sha_matched"])
        self.assertTrue(drift["original_official_build_restored"])
        failed = diag.read_json(self.root / "link-fail-trial/diagnostic-receipt.json")
        self.assertEqual(failed["verdict"], "DIAGNOSTIC_GNU_KBUILD_LINK_FAIL")
        self.assertTrue(failed["original_official_build_restored"])
        self.assertEqual(diag.digest(self.out / "arch/x86/boot/bzImage"), original_gnu_image)
        self.assertFalse((self.work / "build-before-cohort-diagnostic").exists())

    @staticmethod
    def _fake_qemu_lane(*args):
        boot = args[-1]
        put(boot / "p0.log", b"GNU_PASS=1\n")
        return {"lane": "p0", "verdict": "GNU_QEMU_RUNTIME_PASS"}


if __name__ == "__main__":
    unittest.main()
