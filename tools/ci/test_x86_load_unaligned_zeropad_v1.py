#!/usr/bin/env python3
"""x86 Linux type-20 faulting word load: Source -> typed MIR -> GNU AS.

Run with: python3 tools/ci/test_x86_load_unaligned_zeropad_v1.py /path/to/minic
Optional MINIC_X86_FORM73_ORIGINAL_I=path1:path2 replays original Linux .i.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


COMPILER = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else (
    Path(__file__).resolve().parents[2] / "build/release/bin/minic")

SOURCE = r'''unsigned long read_word(const void *addr) {
    unsigned long ret;
    asm volatile(
        "1:\tmov %[mem], %[ret]\n"
        "2:\n"
        " .pushsection \"__ex_table\",\"a\"\n"
        " .balign 4\n"
        " .long (1b) - .\n"
        " .long (2b) - .\n"
        " .long 20 \n"
        " .popsection\n"
        : [ret] "=r" (ret)
        : [mem] "m" (*(unsigned long *)addr));
    return ret;
}
'''


class X86ZeropadTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="minic-x86-form73-")
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)

    def compile(self, name, code, expect_success):
        original = self.dir / (name + ".i")
        assembly = self.dir / (name + ".s")
        original.write_text(code)
        proc = subprocess.run([str(COMPILER), "-S", str(original),
                               "--target=x86_64-linux", "-o", str(assembly)],
                              capture_output=True, text=True, timeout=90)
        if expect_success:
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertTrue(assembly.exists(), "missing GNU assembly")
            return assembly
        self.assertNotEqual(proc.returncode, 0,
                            "invalid Linux inline asm was accepted: " + name)
        return None

    def check_object(self, asm_path):
        source = asm_path.read_text()
        self.assertRegex(source, r"1:\n\s+movq \(%r10\), %rax\n2:\n")
        self.assertRegex(source, r'\.pushsection "__ex_table","a"\n'
                         r'\s+\.balign 4\n\s+\.long \(1b\) - \.\n'
                         r'\s+\.long \(2b\) - \.\n\s+\.long 20\n')
        object_path = asm_path.with_suffix(".o")
        subprocess.run(["as", "--64", str(asm_path), "-o", str(object_path)],
                       check=True, capture_output=True, text=True, timeout=90)
        section = subprocess.run(["readelf", "-x", "__ex_table",
                                  str(object_path)], check=True,
                                 capture_output=True, text=True).stdout
        self.assertIn("14000000", section, "GNU ELF lost type-20 fixup")
        relocations = subprocess.run(["readelf", "-r", str(object_path)],
                                    check=True, capture_output=True,
                                    text=True).stdout
        ex_table = re.search(r"Relocation section '\.rela__ex_table'.*?"
                             r"(?=\nRelocation section|\Z)",
                             relocations, flags=re.S)
        self.assertIsNotNone(ex_table, "missing __ex_table relocations")
        self.assertEqual(ex_table.group().count("R_X86_64_PC32"), 2)

    def test_named_original_form(self):
        self.check_object(self.compile("named", SOURCE, True))

    def test_numbered_original_form(self):
        numbered = SOURCE.replace("%[mem], %[ret]", "%1, %0").replace(
            '[ret] "=r"', '"=r"').replace('[mem] "m"', '"m"')
        self.check_object(self.compile("numbered", numbered, True))

    def test_reject_wrong_exception_type(self):
        self.compile("wrong_type", SOURCE.replace(".long 20", ".long 3"), False)

    def test_reject_wrong_output_constraint(self):
        self.compile("wrong_constraint", SOURCE.replace('"=r"', '"=m"'), False)

    def test_reject_wrong_scalar_width(self):
        self.compile("wrong_width", SOURCE.replace("unsigned long ret;",
                        "unsigned int ret;"), False)

    def test_reject_unproven_memory_clobber(self):
        self.compile("wrong_clobber", SOURCE.replace(
            ': [mem] "m" (*(unsigned long *)addr));',
            ': [mem] "m" (*(unsigned long *)addr) : "memory");'), False)

    def test_real_linux_preprocessed_inputs(self):
        inputs = os.environ.get("MINIC_X86_FORM73_ORIGINAL_I", "")
        if not inputs:
            self.skipTest("set MINIC_X86_FORM73_ORIGINAL_I for original Linux .i")
        for index, path in enumerate(inputs.split(":")):
            with self.subTest(path=path):
                raw = Path(path).resolve()
                self.assertTrue(raw.is_file(), raw)
                assembly = self.dir / ("linux_" + str(index) + ".s")
                proc = subprocess.run([str(COMPILER), "-S", str(raw),
                    "--target=x86_64-linux", "-fshort-wchar",
                    "-funsigned-char", "-o", str(assembly)],
                    capture_output=True, text=True, timeout=180)
                self.assertEqual(proc.returncode, 0, proc.stderr)
                self.check_object(assembly)


if __name__ == "__main__":
    unittest.main(verbosity=2)
