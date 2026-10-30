#!/usr/bin/env python3
"""Fail-closed tests for separate, bounded AArch64 QEMU early-boot tracing."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SOURCE = Path(__file__).with_name("linux-three-isa-aarch64-early-boot-trace-v1.py")
spec = importlib.util.spec_from_file_location("minic_aarch_early_trace", SOURCE)
assert spec and spec.loader
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class EarlyBootTraceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="aarch-early-boot-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.qemu = self.root / "qemu-system-aarch64"
        self.mode = self.root / "fake-mode"
        self.qemu.write_text(
            "#!/usr/bin/env python3\n"
            "import pathlib,sys,time\n"
            "args=sys.argv[1:]\n"
            "assert args[args.index('-d')+1]=='int,guest_errors'\n"
            "target=pathlib.Path(args[args.index('-D')+1])\n"
            f"mode=pathlib.Path({str(self.mode)!r}).read_text()\n"
            "if mode=='timeout':time.sleep(10)\n"
            "if mode=='oversize':target.write_bytes(b'X'*(1024*1024))\n"
            "else:target.write_text('Exception raised\\nPC=0xffff000010081234\\n')\n"
            "print('diagnostic console only',flush=True)\n")
        self.qemu.chmod(0o755)
        self.mode.write_text("normal")
        self.image = self.root / "candidate-Image"
        header = bytearray(64)
        header[8:16] = (0x80000).to_bytes(8, "little")
        header[16:24] = (0x400000).to_bytes(8, "little")
        header[56:60] = b"ARM\x64"
        self.image.write_bytes(bytes(header) + b"MINIC_CANDIDATE")
        self.initramfs = self.root / "initramfs.cpio.gz"
        self.initramfs.write_bytes(b"same oracle initramfs")
        self.failed = self.root / "p0.log"
        self.failed.touch()
        self.gnu = self.root / "gnu-qemu-receipt.json"
        oracle = trace.module()
        p0 = oracle.boot_args("aarch64", self.qemu.resolve(), self.image.resolve(),
                              self.initramfs.resolve(), None, "p0", "2G")
        p1 = oracle.boot_args("aarch64", self.qemu.resolve(), self.image.resolve(),
                              self.initramfs.resolve(), None, "p1", "2G")
        self.receipt = {"target": "aarch64", "verdict": "GNU_QEMU_P0_P1_PASS_NOT_MINIC",
                        "image_sha256": hashlib.sha256(b"GNU_ORIGINAL_IMAGE").hexdigest(),
                        "initramfs_sha256": sha(self.initramfs),
                        "lanes": [{"lane": "p0", "qemu_command": p0},
                                  {"lane": "p1", "qemu_command": p1}]}
        self.gnu.write_text(json.dumps(self.receipt))
        self.output = self.root / "diagnostic"

    def call(self, **overrides):
        args = {"image": self.image, "initramfs": self.initramfs,
                "failed_p0": self.failed, "gnu_receipt": self.gnu,
                "output": self.output, "qemu": self.qemu,
                "timeout": 3, "max_bytes": 65536}
        args.update(overrides)
        return trace.run(**args)

    def test_runs_same_qemu_args_and_does_not_modify_candidate(self):
        before = {p: sha(p) for p in (self.image, self.initramfs,
                                      self.failed, self.gnu)}
        report = self.call()
        self.assertEqual(report["verdict"], trace.VERDICT)
        self.assertTrue(report["original_inputs_unchanged"])
        self.assertTrue(report["image_header"]["arm64_magic_valid"])
        self.assertEqual(report["guest_pc_or_elr_samples"], ["0xffff000010081234"])
        self.assertIn("Exception raised", report["exception_or_reset_samples"])
        self.assertEqual(report["diagnostic_exit_code"], 0)
        self.assertFalse(report["diagnostic_timed_out"])
        self.assertEqual(report["diagnostic_qemu_command"][:-4],
                         report["original_qemu_command"])
        self.assertEqual(report["diagnostic_qemu_command"][-4:-2],
                         ["-d", "int,guest_errors"])
        self.assertEqual(before, {p: sha(p) for p in before})
        self.assertEqual(self.failed.stat().st_size, 0)
        with self.assertRaises(FileExistsError):
            self.call()

    def test_rejects_nonempty_serial_or_forged_receipt(self):
        self.failed.write_text("Oops: unexpected serial evidence\n")
        with self.assertRaises(trace.DiagnosticError):
            self.call()
        self.failed.write_text("")
        self.receipt["image_sha256"] = sha(self.image)
        self.gnu.write_text(json.dumps(self.receipt))
        with self.assertRaises(trace.DiagnosticError):
            self.call()
        self.receipt["image_sha256"] = hashlib.sha256(b"GNU_ORIGINAL_IMAGE").hexdigest()
        self.receipt["lanes"][0]["qemu_command"][-1] = "console=ttyS0"
        self.gnu.write_text(json.dumps(self.receipt))
        with self.assertRaises(trace.DiagnosticError):
            self.call()
        self.assertFalse(self.output.exists())

    def test_timeout_is_bounded_and_never_runtime_pass(self):
        self.mode.write_text("timeout")
        report = self.call(timeout=2)
        self.assertTrue(report["diagnostic_timed_out"])
        self.assertLess(report["diagnostic_elapsed_seconds"], 4)
        self.assertEqual(report["verdict"], trace.VERDICT)
        self.assertFalse((self.output / "minic-qemu-receipt.json").exists())

    def test_trace_file_limit_is_enforced(self):
        self.mode.write_text("oversize")
        report = self.call()
        self.assertLessEqual(report["debug_trace_bytes"], 65536)
        self.assertNotEqual(report["diagnostic_exit_code"], 0)
        self.assertEqual(report["verdict"], trace.VERDICT)

    def test_read_only_original_gnu_nm_pc_mapping(self):
        vmlinux = self.root / "vmlinux"
        elf = bytearray(64)
        elf[:6] = b"\x7fELF\x02\x01"
        elf[18:20] = (183).to_bytes(2, "little")
        vmlinux.write_bytes(elf)
        nm = self.root / "aarch64-linux-gnu-nm"
        nm.write_text("#!/bin/sh\n"
                      "printf 'ffff000010080000 T _text\\n'\n"
                      "printf 'ffff000010081230 T early_fault_function\\n'\n")
        nm.chmod(0o755)
        before = sha(vmlinux)
        found = trace.resolve_kernel_symbols(vmlinux, nm,
                                             ["0xffff000010081234", "0x40080000"])
        self.assertEqual(found["kernel_symbols_seen"], 2)
        self.assertEqual(found["absolute_vma_matches"][0]["absolute_vma_match"]["symbol"],
                         "early_fault_function")
        self.assertEqual(found["absolute_vma_matches"][0]["absolute_vma_match"]["offset"],
                         "0x4")
        self.assertIsNone(found["absolute_vma_matches"][1]["absolute_vma_match"])
        self.assertEqual(before, sha(vmlinux))


if __name__ == "__main__":
    unittest.main()
