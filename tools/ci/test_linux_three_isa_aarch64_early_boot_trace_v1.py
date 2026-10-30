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
        runtime_vbar = 0xffffa8a0bec29800
        slide = 0x40000000
        fault_elr = 0xffffa8a0c34173a0
        vector_vma = runtime_vbar - slide
        instruction_vma = fault_elr - slide
        nm.write_text("#!/bin/sh\n"
                      f"printf '{vector_vma:016x} T vectors\\n'\n"
                      f"printf '{instruction_vma - 0x34:016x} T early_fault_function\\n'\n")
        nm.chmod(0o755)
        before = sha(vmlinux)
        # Exact original #41118175799 exception-entry trace grammar.
        events = ("Taking exception 5 [IRQ] on CPU 0\n"
                  "...from EL1 to EL1\n"
                  "...with ESR 0x0/0x3890000\n"
                  "...with ELR 0xffffa8a0bec2e9fc\n"
                  "...to EL1 PC 0xffffa8a0bec29a80 PSTATE 0x3c5\n"
                  "Taking exception 4 [Data Abort] on CPU 0\n"
                  "...from EL1 to EL1\n"
                  "...with ESR 0x25/0x96000047\n"
                  "...with FAR 0xffff800080024b40\n"
                  "...with ELR 0xffffa8a0c34173a0\n"
                  "...to EL1 PC 0xffffa8a0bec29a00 PSTATE 0x3c5\n")
        facts = trace.el1_vector_events(events)
        self.assertEqual(facts["irq_entry_pcs"], ["0xffffa8a0bec29a80"])
        self.assertEqual(facts["sync_entry_pcs"], ["0xffffa8a0bec29a00"])
        self.assertEqual(facts["data_abort_facts"][0]["far"], "0xffff800080024b40")
        self.assertEqual(facts["data_abort_facts"][0]["esr"], "0x25/0x96000047")
        found = trace.strict_vector_kaslr_proof(facts, vmlinux, nm)
        self.assertEqual(found["status"], "KASLR_SLIDE_PROVEN_TWO_EL1_VECTOR_ANCHORS")
        self.assertEqual(found["runtime_vbar_el1"], "0xffffa8a0bec29800")
        self.assertEqual(found["kaslr_slide"], "0x40000000")
        self.assertEqual(found["kernel_symbols_seen"], 2)
        self.assertEqual(found["fault_symbolications"][0]["fault_instruction_elr"][
            "nearest_executable_symbol"]["name"], "early_fault_function")
        self.assertEqual(found["fault_symbolications"][0]["fault_instruction_elr"][
            "nearest_executable_symbol"]["offset"], "0x34")
        self.assertTrue(found["fault_symbolications"][0]["far_is_data_address_not_symbolicated"])
        self.assertEqual(trace.strict_vector_kaslr_proof(facts)["status"], "UNKNOWN")
        # Never infer a KASLR slide from one entry, conflicting vector PCs,
        # a misaligned VBAR, or an ELF vectors symbol with wrong alignment.
        missing_sync = dict(facts, sync_entry_pcs=[])
        self.assertEqual(trace.strict_vector_kaslr_proof(missing_sync, vmlinux, nm)["status"],
                         "UNKNOWN")
        differing_sync = dict(facts, sync_entry_pcs=["0xffffa8a0bec29b00"])
        self.assertEqual(trace.strict_vector_kaslr_proof(differing_sync, vmlinux, nm)["status"],
                         "UNKNOWN")
        two_irqs = dict(facts, irq_entry_pcs=["0xffffa8a0bec29a80", "0xffffa8a0bec2aa80"])
        self.assertEqual(trace.strict_vector_kaslr_proof(two_irqs, vmlinux, nm)["status"],
                         "UNKNOWN")
        nm.write_text("#!/bin/sh\n"
                      f"printf '{vector_vma + 0x800:016x} T vectors\\n'\n")
        self.assertEqual(trace.strict_vector_kaslr_proof(facts, vmlinux, nm)["status"],
                         "UNKNOWN")
        self.assertEqual(before, sha(vmlinux))


if __name__ == "__main__":
    unittest.main()
