#!/usr/bin/env python3
"""Original Linux type-safe BTCQ: Source -> MachineIR -> GNU AS.

Usage: python3 tools/ci/test_x86_lowlevel_btcq_v1.py /path/to/minic-cc
Optional original Linux .i files: MINIC_X86_BTCQ_ORIGINAL_I=path1:path2
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


COMPILER = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else (
    Path(__file__).resolve().parents[2] / "build/release/bin/minic-cc")

SOURCE = r'''static volatile unsigned long words[2];
static void flip(unsigned long bit) {
    asm volatile("btcq %1,%0" : : "m" (*(volatile long *)words),
                 "Ir" (bit) : "memory");
}
int main(void) {
    flip(1);
    flip(1);
    flip(65);
    return words[0] == 0 && words[1] == 2 ? 0 : 1;
}
'''


class LinuxBTCQ(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="minic-x86-btcq-")
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def compile(self, name, source, expect_success):
        original = self.dir / (name + ".i")
        assembly = self.dir / (name + ".s")
        original.write_text(source)
        run = subprocess.run([str(COMPILER), "-S", str(original),
                              "--target=x86_64-linux", "-o", str(assembly)],
                             capture_output=True, text=True, timeout=90)
        if not expect_success:
            self.assertNotEqual(run.returncode, 0,
                                "invalid BTCQ form admitted: " + name)
            return None
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertTrue(assembly.is_file())
        self.assertRegex(assembly.read_text(), r"\bbtcq %r11, \(%r10\)")
        return assembly

    def assemble(self, assembly):
        obj = assembly.with_suffix(".o")
        subprocess.run(["as", "--64", str(assembly), "-o", str(obj)],
                       check=True, capture_output=True, text=True, timeout=90)
        disasm = subprocess.run(["objdump", "-dr", str(obj)], check=True,
                                capture_output=True, text=True).stdout
        self.assertRegex(disasm, r"\bbtc\s+%r11,\(%r10\)")
        return obj

    def test_original_numbered_form_native_runtime(self):
        obj = self.assemble(self.compile("original", SOURCE, True))
        exe = self.dir / "btc-test"
        subprocess.run(["gcc", "-no-pie", str(obj), "-o", str(exe)],
                       check=True, capture_output=True, text=True, timeout=90)
        run = subprocess.run([str(exe)], capture_output=True, text=True,
                             timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_reject_narrow_memory_word(self):
        self.compile("narrow_memory", SOURCE.replace(
            "*(volatile long *)words", "*(volatile int *)words"), False)

    def test_reject_narrow_bit_index(self):
        self.compile("narrow_index", SOURCE.replace(
            "flip(unsigned long bit)", "flip(unsigned int bit)"), False)

    def test_reject_missing_memory_clobber(self):
        self.compile("no_barrier", SOURCE.replace(
            '"Ir" (bit) : "memory"', '"Ir" (bit)'), False)

    def test_reject_unqualified_input_memory(self):
        self.compile("bad_constraint", SOURCE.replace(
            '"m" (*(volatile long *)words)',
            '"=m" (*(volatile long *)words)'), False)

    def test_reject_unqualified_template(self):
        self.compile("bad_template", SOURCE.replace(
            "btcq %1,%0", "btcq %1,%0\\n\\tud2"), False)

    def test_original_linux_owners(self):
        originals = os.environ.get("MINIC_X86_BTCQ_ORIGINAL_I", "")
        if not originals:
            self.skipTest("set MINIC_X86_BTCQ_ORIGINAL_I for genuine Kbuild .i")
        for index, raw in enumerate(originals.split(":")):
            with self.subTest(path=raw):
                origin = Path(raw).resolve()
                self.assertTrue(origin.is_file(), origin)
                assembly = self.dir / ("linux" + str(index) + ".s")
                run = subprocess.run([str(COMPILER), "-S", str(origin),
                    "--target=x86_64-linux", "-o", str(assembly)],
                    capture_output=True, text=True, timeout=180)
                self.assertEqual(run.returncode, 0, run.stderr)
                self.assertIn("btcq %r11, (%r10)", assembly.read_text())
                self.assemble(assembly)


if __name__ == "__main__":
    unittest.main(verbosity=2)
