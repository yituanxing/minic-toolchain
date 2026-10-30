#!/usr/bin/env python3
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("trace", Path(__file__).with_name(
    "linux-three-isa-x86-early-boot-trace-v1.py"))
t = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t)


class X86TraceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.qemu = self.root / "qemu-system-x86_64"
        self.qemu.write_text("#!/usr/bin/env python3\nimport sys,pathlib\n"
            "a=sys.argv;assert a[a.index('-d')+1]=='int,cpu_reset,guest_errors'\n"
            "pathlib.Path(a[a.index('-D')+1]).write_text('CPU Reset (CPU 0)\\n'"
            "'EIP=0000fff0 EFL=00000002\\ncheck_exception old: 0xd new 0xd\\n'"
            "'     0: v=0d e=0000 i=0 cpl=0 IP=0010:0000000000101234 pc=0000000000101234\\n'"
            "'RIP=0000000000101234 RAX=0000000000000000\\nTriple fault\\n')\n")
        self.qemu.chmod(0o755)
        self.image = self.root / "bzImage"
        self.image.write_bytes(b"actual candidate")
        self.initramfs = self.root / "initramfs"
        self.initramfs.write_bytes(b"initramfs")
        self.failed = self.root / "p0.log"
        self.failed.write_bytes(b'\x1bc\x1b[?7l\x1b[2J\x1b[0mSeaBIOS (version test)\r\nBooting from ROM...\r\n\x1bc\x1b[?7l\x1b[2J')
        oracle = t.common.module()
        self.receipt = dict(target="x86_64", verdict="GNU_QEMU_P0_P1_PASS_NOT_MINIC",
            image_sha256=hashlib.sha256(b"GNU").hexdigest(),
            initramfs_sha256=t.common.sha(self.initramfs), lanes=[dict(lane=n,
                qemu_command=oracle.boot_args("x86_64", self.qemu, self.image,
                                             self.initramfs, None, n, "2G")) for n in ("p0", "p1")])
        self.gnu = self.root / "gnu.json"
        self.gnu.write_text(json.dumps(self.receipt))
        self.args = dict(image=self.image, initramfs=self.initramfs, failed_p0=self.failed,
                         gnu_receipt=self.gnu, output=self.root / "diagnostic", qemu=self.qemu,
                         timeout=2, max_bytes=65536)

    def test_round_trip_evidence_only(self):
        report = t.run(**self.args)
        self.assertEqual(report["verdict"], t.VERDICT)
        self.assertTrue(report["original_inputs_unchanged"])
        self.assertTrue(report["triple_fault_observed"])
        self.assertEqual(report["exception_vector_counts"], {"0d": 1})
        self.assertEqual(report["cpu_reset_dump_count"], 1)
        self.assertFalse(report["repeated_reset_observed"])
        self.assertIn("0000000000101234", report["observed_pc_samples"])
        self.assertTrue(report["x86_register_dump_tail"])
        self.assertEqual(report["diagnostic_qemu_command"][:-4], report["original_qemu_command"])
        with self.assertRaises(FileExistsError):
            t.run(**self.args)

    def test_non_bios_log_rejected(self):
        for data in (b"", b"Linux version", self.failed.read_bytes() + b"Linux version 6.6\n"):
            self.failed.write_bytes(data)
            with self.assertRaises(t.DiagnosticError):
                t.run(**self.args)

    def test_forged_oracle_rejected(self):
        self.receipt["target"] = "aarch64"
        self.gnu.write_text(json.dumps(self.receipt))
        with self.assertRaises(t.DiagnosticError):
            t.run(**self.args)

    def test_changed_command_rejected(self):
        self.receipt["lanes"][0]["qemu_command"] += ["-no-shutdown"]
        self.gnu.write_text(json.dumps(self.receipt))
        with self.assertRaises(t.DiagnosticError):
            t.run(**self.args)

    def test_timeout_kills_diagnostic_only(self):
        self.qemu.write_text("#!/usr/bin/env python3\nimport time\ntime.sleep(10)\n")
        report = t.run(**self.args)
        self.assertTrue(report["diagnostic_timed_out"])
        self.assertTrue(report["original_inputs_unchanged"])
        self.assertFalse(report["triple_fault_observed"])
        self.assertTrue(report["no_event_does_not_prove_absence"])

    def test_byte_limit(self):
        self.qemu.write_text("#!/usr/bin/env python3\nimport pathlib,sys\n"
                            "pathlib.Path(sys.argv[sys.argv.index('-D')+1]).write_bytes(b'X'*1048576)\n")
        report = t.run(**self.args)
        self.assertLessEqual(report["trace_bytes"], 65536)
        self.assertTrue(report["trace_at_byte_limit"])
        self.assertTrue(report["original_inputs_unchanged"])


if __name__ == "__main__":
    unittest.main()
