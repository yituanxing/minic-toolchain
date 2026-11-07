#!/usr/bin/env python3
"""Small, no-Linux smoke tests for diagnostic-only x86 cohort object swapping."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import struct
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
            self.assertEqual(diag.select_owners(self.rows, "gnu-plus-compressed",
                                                ["init/main.o"]),
                             [*([row["object"] for row in self.rows[:2]]), "init/main.o"])
            for mode, extras in (("minic-except-compressed", ["init/main.o"]),
                                 ("gnu-plus-compressed", ["init/main.o", "init/main.o"]),
                                 ("gnu-plus-compressed", [self.rows[0]["object"]]),
                                 ("gnu-plus-compressed", ["../escape.o"]),
                                 ("gnu-plus-compressed", ["not/a-real-owner.o"])):
                with self.assertRaises(diag.DiagnosticError):
                    diag.select_owners(self.rows, mode, extras)
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
                                  mode="gnu-plus-compressed", extra_owners_file=None, link_timeout=900,
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
            extras = self.root / "early-extra-owners.json"
            put(extras, b'["init/main.o"]')
            args.mode = "gnu-plus-compressed"
            args.extra_owners_file = extras
            args.trial_dir = self.root / "extra-trial"
            extra_rc = diag.probe(args)
            args.extra_owners_file = None
            drift_case["enabled"] = True
            args.mode = "gnu-plus-compressed"
            args.trial_dir = self.root / "drift-trial"
            drift_rc = diag.probe(args)
            drift_case["enabled"] = False
            link_fail_case["enabled"] = True
            args.trial_dir = self.root / "link-fail-trial"
            fail_rc = diag.probe(args)
        self.assertEqual(rc, 0)
        self.assertEqual(extra_rc, 0)
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
        extra = diag.read_json(self.root / "extra-trial/diagnostic-receipt.json")
        self.assertEqual(extra["selected_minic_owner_count"], 3)
        self.assertEqual(extra["extra_owner_selection"], ["init/main.o"])
        self.assertEqual(extra["extra_owner_selection_sha256"], diag.digest(extras))
        self.assertFalse(extra["strict_qualification_allowed"])
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


class LoongLiveEpochNegativeTests(unittest.TestCase):
    def test_vdso_pt_load_measures_mapping_span_not_text_size(self):
        with tempfile.TemporaryDirectory(prefix="minic-loong-vdso-phdr-") as tmp:
            path = Path(tmp) / "vdso.so.dbg"
            data = bytearray(18000)
            data[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<H", data, 18, 258)
            struct.pack_into("<Q", data, 32, 64)
            struct.pack_into("<HH", data, 54, 56, 1)
            struct.pack_into("<IIQQQQQQ", data, 64, 1, 5, 0, 0, 0,
                             16536, 16536, 16384)
            put(path, bytes(data))
            result = diag.vdso_elf_layout(path)
            self.assertEqual(result["pt_load_span_bytes"], 16536)
            self.assertEqual(result["required_16k_mapping_pages"], 2)
            self.assertEqual(result["required_4k_mapping_pages"], 5)
            self.assertNotIn("guest_mapping_page_count", result)
            data[18] = 62  # A foreign machine cannot be treated as Loong evidence.
            put(path, bytes(data))
            self.assertEqual(diag.vdso_elf_layout(path)["verdict"], "INVALID_LOONG_PHDR")

    def test_exact_owner_lists_and_authenticated_live_vdso_sha(self):
        with tempfile.TemporaryDirectory(prefix="minic-loong-live-epoch-") as tmp:
            root = Path(tmp)
            work, candidate = root / "gnu-work", root / "candidate"
            image_sha = put(work / "build/arch/loongarch/boot/vmlinux.efi", b"candidate efi")
            put(candidate / "candidate-receipt.json", json.dumps({
                "schema": "minic-three-isa-candidate-build-v1", "target": "loongarch64",
                "candidate_image_sha256": image_sha}).encode())
            names = ("arch/loongarch/kernel/vdso.o", "fs/exec.o")
            rows = [{"object": name, "sha256": hashlib.sha256(
                ("frozen-i:" + name).encode()).hexdigest()} for name in names]
            self.assertEqual(diag.select_owners(rows, "gnu-plus-list", [names[0]]), [names[0]])
            self.assertEqual(diag.select_owners(rows, "minic-except-list", [names[0]]), [names[1]])
            for unsafe in ([], [names[0], names[0]], ["fs/unknown.o"], list(names)):
                with self.assertRaises(diag.DiagnosticError):
                    diag.select_owners(rows, "gnu-plus-list", unsafe)
            frozen = {name: hashlib.sha256(("frozen:" + name).encode()).hexdigest()
                      for name in names}
            final = {}
            for name in names:
                value = ("live:" if name == names[0] else "frozen:") + name
                final[name] = put(work / "build" / name, value.encode())
            target = candidate / "postlink-live-gnu-e-minic-c/arch/loongarch/kernel/vdso"
            live_i = put(target.with_suffix(".live.gnu.i"), b"late-generated-offsets")
            live_o = put(target.with_suffix(".live.minic.o"), ("live:" + names[0]).encode())
            self.assertEqual(live_o, final[names[0]])
            sha_lines = "".join(f"{sha}  {name}\n" for name, sha in final.items()).encode()
            manifest_sha = put(candidate / "postlink-target-elf64-objects.sha256", sha_lines)
            report = {"target": "loongarch64", "verdict": "POSTLINK_ALL_C_MINIC_SHA_PASS",
                      "postlink_target_object_manifest_sha256": manifest_sha}
            receipt_sha = put(candidate / "postlink-c-owner-census.json",
                              json.dumps(report).encode())
            regenerated = {"schema": "minic-three-isa-live-kbuild-c-regeneration-v1",
                           "verdict": "LIVE_MINIC_C_SHA_POSTLINK_PASS",
                           "final_postlink_census_sha256": receipt_sha,
                           "live_recompiled": [{"object": names[0],
                               "original_frozen_i_sha256": rows[0]["sha256"],
                               "live_gnu_i_sha256": live_i,
                               "live_minic_object_sha256": live_o,
                               "live_input_differs_from_original_frozen": True,
                               "actual_logged_gnu_cc": True}]}
            put(candidate / "postlink-live-gnu-e-minic-c-receipt.json",
                json.dumps(regenerated).encode())
            authenticated, evidence = diag.verify_final_candidate_live_owners(
                rows, work, candidate, frozen)
            self.assertEqual(authenticated, final)
            self.assertEqual(evidence["frozen_live_source_i_differences"], [names[0]])
            self.assertEqual(evidence["frozen_live_object_sha_differences"], [names[0]])
            # A current frozen .o cannot silently replace late MiniC vDSO.
            put(work / "build" / names[0], ("frozen:" + names[0]).encode())
            with self.assertRaises(diag.DiagnosticError):
                diag.verify_final_candidate_live_owners(rows, work, candidate, frozen)
            put(work / "build" / names[0], ("live:" + names[0]).encode())
            # A changed regenerated GNU .i is a different source epoch.
            put(target.with_suffix(".live.gnu.i"), b"tampered-header")
            with self.assertRaises(diag.DiagnosticError):
                diag.verify_final_candidate_live_owners(rows, work, candidate, frozen)


if __name__ == "__main__":
    unittest.main()
