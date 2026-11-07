#!/usr/bin/env python3
"""x86 Linux IPv6 carry-chain: physical MachineIR and authentic GNU-AS check.

Optional ORIGINAL_I and ORIGINAL_CMD are the SHA-attested frozen GNU Kbuild
arch/x86/lib/csum-wrappers_64.o owner, not a recreated C translation unit.
"""

import hashlib
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build/debug/bin/minic-cc"
SOURCE = r'''
typedef unsigned long long u64;
u64 minic_ipv6_sum(const u64 *saddr, const u64 *daddr, u64 rest) {
    u64 sum64;
    asm("\taddq (%[saddr]),%[sum]\n"
        "\tadcq 8(%[saddr]),%[sum]\n"
        "\tadcq (%[daddr]),%[sum]\n"
        "\tadcq 8(%[daddr]),%[sum]\n"
        "\tadcq $0,%[sum]\n"
        : [sum] "=r" (sum64)
        : "[sum]" (rest), [saddr] "r" (saddr), [daddr] "r" (daddr));
    return sum64;
}
'''
DRIVER = r'''
#include <stdint.h>
#include <stdio.h>
extern uint64_t minic_ipv6_sum(const uint64_t *, const uint64_t *, uint64_t);
extern uint64_t gnu_ipv6_sum(const uint64_t *, const uint64_t *, uint64_t);
static uint64_t state = UINT64_C(0x418e51a9982bcd37);
static uint64_t rnd(void) {
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    return state;
}
int main(void) {
    uint64_t a[2],b[2],rest;
    for (unsigned i=0; i<1024; ++i) {
        a[0]=rnd();a[1]=rnd();b[0]=rnd();b[1]=rnd();rest=rnd();
        if (i == 0) { a[0]=a[1]=b[0]=b[1]=rest=UINT64_MAX; }
        if (i == 1) { a[0]=a[1]=b[0]=b[1]=rest=0; }
        uint64_t m=minic_ipv6_sum(a,b,rest),g=gnu_ipv6_sum(a,b,rest);
        if (m!=g) { fprintf(stderr,"IPv6 carry mismatch i=%u m=%llu g=%llu\n",
                           i,(unsigned long long)m,(unsigned long long)g); return 1; }
    }
    puts("X86_IPV6_CARRY_GNU_DIFF=PASS vectors=1024");
    return 0;
}
'''


def invoke(*args, cwd=None):
    return subprocess.run([str(a) for a in args], cwd=cwd,
                          text=True, capture_output=True, timeout=120)


class LinuxIPv6Checksum(unittest.TestCase):
    def compile_minic(self, source, directory):
        inp = directory / "source.i"
        asm = directory / "minic.s"
        inp.write_text(source)
        return invoke(COMPILER, "--target=x86_64-linux", "-mno-sse",
                      "-S", inp, "-o", asm), asm

    def test_native_gnu_carry_differential(self):
        with tempfile.TemporaryDirectory(prefix="minic-x86-ipv6-") as temp:
            d=Path(temp)
            cc,asm=self.compile_minic(SOURCE,d)
            self.assertEqual(cc.returncode,0,cc.stderr[-1200:])
            text=asm.read_text()
            self.assertIn("adcq 8(%r10), %rax",text)
            self.assertIn("adcq 8(%r11), %rax",text)
            mini=d/"minic.o"
            result=invoke("gcc","-mno-sse","-mno-red-zone","-mcmodel=kernel",
                          "-x","assembler","-c",asm,"-o",mini)
            self.assertEqual(result.returncode,0,result.stderr)
            gnu_source=d/"gnu.c"
            gnu_source.write_text(SOURCE.replace("minic_ipv6_sum", "gnu_ipv6_sum"))
            gnu=d/"gnu.o"
            result=invoke("gcc","-O2","-mno-sse","-fno-pie","-c",gnu_source,"-o",gnu)
            self.assertEqual(result.returncode,0,result.stderr)
            driver=d/"driver.c"
            driver.write_text(DRIVER)
            executable=d/"check"
            result=invoke("gcc","-O2","-no-pie",driver,mini,gnu,"-o",executable)
            self.assertEqual(result.returncode,0,result.stderr)
            result=invoke(executable)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn("X86_IPV6_CARRY_GNU_DIFF=PASS",result.stdout)

    def test_invalid_template_and_tie_fail_closed(self):
        invalid=[
            SOURCE.replace("adcq 8(%[daddr])", "addq 8(%[daddr])"),
            SOURCE.replace('"[sum]" (rest)', '"r" (rest)'),
            SOURCE.replace('asm("\\taddq', 'asm volatile("\\taddq'),
            SOURCE.replace('"r" (daddr));', '"r" (daddr) : "memory");'),
        ]
        with tempfile.TemporaryDirectory(prefix="minic-x86-ipv6-negative-") as temp:
            d=Path(temp)
            for i,source in enumerate(invalid):
                with self.subTest(case=i):
                    case=d/str(i)
                    case.mkdir()
                    cc,_=self.compile_minic(source,case)
                    self.assertNotEqual(cc.returncode,0,source)

    def test_exact_original_gnu_owner(self):
        orig=os.getenv("MINIC_X86_IPV6_ORIGINAL_I")
        cmd=os.getenv("MINIC_X86_IPV6_ORIGINAL_CMD")
        if not (orig and cmd):
            self.skipTest("set original GNU Kbuild .i and .o.cmd for exact owner replay")
        source=Path(orig).resolve(strict=True)
        command=Path(cmd).resolve(strict=True)
        self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),
            "95011c32a01c9e135fe7a90a12f15a603eb2ce98cc56b145ea7a6d30746709f6")
        self.assertEqual(hashlib.sha256(command.read_bytes()).hexdigest(),
            "0d19a7fdc9d927fe80ce730ff442a224e7df760db3d91abd1bb09bc0620c6d22")
        oracle=runpy.run_path(str(ROOT / "tools/ci/linux-three-isa-freeze-i-v1.py"))
        tokens=oracle["gnu_command_tokens"](command.read_text(),"gcc")
        flags=oracle["gnu_assembler_flags"](tokens)
        self.assertTrue(oracle["valid_assembler_flags"](flags))
        with tempfile.TemporaryDirectory(prefix="minic-x86-ipv6-original-") as temp:
            d=Path(temp)
            asm,obj=d/"original.s",d/"original.o"
            cc=invoke(COMPILER,"--target=x86_64-linux","-mno-sse",
                      "-S",source,"-o",asm)
            self.assertEqual(cc.returncode,0,cc.stderr[-1200:])
            gnu=invoke("gcc",*flags,"-x","assembler","-c",asm,"-o",obj)
            self.assertEqual(gnu.returncode,0,gnu.stderr[-1200:])
            self.assertEqual(obj.read_bytes()[:4],b"\x7fELF")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]],verbosity=2)
