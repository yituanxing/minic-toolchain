#!/usr/bin/env python3
"""Regression tests for replaying frozen Linux Kbuild ABI flags in phase traces."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("linux-perf-phase-trace-v1.py")
KBUILD_FLAGS = ("-march=rv64i", "-mabi=lp64", "-fshort-wchar", "-funsigned-char")
STAGES = ("parse", "normalize", "core-set", "core-validate", "layout", "codegen")


class PhaseCompilerFlagsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "frozen.i"
        self.source.write_text("int frozen_owner;\n")

    def compiler(self, name, different_assembly=False):
        path = self.root / name
        assembly = "different" if different_assembly else "same"
        path.write_text(
            "#!" + sys.executable + "\n"
            "import pathlib, sys\n"
            "args = sys.argv[1:]\n"
            f"wanted = {KBUILD_FLAGS!r}\n"
            "if args.index('-S') + 1 >= len(args) or not args[args.index('-S') + 1].endswith('.i'):\n"
            "    print('minic-cc requires the source immediately after -S', file=sys.stderr)\n"
            "    sys.exit(18)\n"
            "if '-S' not in args or any(args.count(flag) != 1 for flag in wanted):\n"
            "    print('missing exact Kbuild flags', file=sys.stderr)\n"
            "    sys.exit(17)\n"
            "dest = pathlib.Path(args[args.index('-o') + 1])\n"
            f"dest.write_text({assembly!r} + '\\n')\n"
            f"for stage in {STAGES!r}:\n"
            "    print(f'MINIC_BOOTSTRAP_TRACE stage={stage} state=begin functions=1', file=sys.stderr, flush=True)\n"
            "    print(f'MINIC_BOOTSTRAP_TRACE stage={stage} state=end-ok functions=1', file=sys.stderr, flush=True)\n"
        )
        path.chmod(0o755)
        return path

    def trace(self, baseline, candidate, flags=KBUILD_FLAGS):
        cmd = [sys.executable, str(SCRIPT), "--input", str(self.source),
               "--baseline", str(baseline), "--candidate", str(candidate),
               "--out", str(self.root / "results")]
        cmd.extend("--compiler-flag=" + flag for flag in flags)
        return subprocess.run(cmd, text=True, capture_output=True, timeout=20)

    def test_exact_abi_flags_on_both_compilers_and_assembly_sha(self):
        old = self.compiler("old")
        new = self.compiler("new")
        result = self.trace(old, new)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("MINIC_PHASE_TRACE=PASS", result.stdout)
        report = json.loads((self.root / "results" / "phases.json").read_text())
        self.assertEqual(report["compiler_flags"], list(KBUILD_FLAGS))
        self.assertEqual(report["results"]["before"]["assembly_sha"],
                         report["results"]["after"]["assembly_sha"])
        self.assertEqual((self.root / "results" / "before.s").read_bytes(),
                         (self.root / "results" / "after.s").read_bytes())

    def test_missing_original_abi_flag_is_rejected(self):
        result = self.trace(self.compiler("old"), self.compiler("new"), KBUILD_FLAGS[:-1])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("trace compiler failed rc=17", result.stderr)

    def test_different_target_assembly_is_rejected(self):
        result = self.trace(self.compiler("old"),
                            self.compiler("new", different_assembly=True))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("different assembly bytes", result.stderr)

    def test_legacy_self_test_still_passes(self):
        result = subprocess.run([sys.executable, str(SCRIPT), "--self-test"],
                                text=True, capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("MINIC_PHASE_TRACE_SELFTEST=PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
