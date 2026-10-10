#!/usr/bin/env python3
"""Offline guard tests; never require a GNU Linux kernel or QEMU."""
from __future__ import annotations
import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("linux-runtime-kbuild-regeneration-guard-v1.py")
spec = importlib.util.spec_from_file_location("regen", SCRIPT)
mod = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(mod)


class RegenTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.out = self.root / "out"
        self.minic = self.root / "minic"
        self.headers = self.out / "include/generated"
        self.headers.mkdir(parents=True)
        (self.out / "arch/riscv/include/generated/asm").mkdir(parents=True)
        (self.headers / "vdso-offsets.h").write_text("#define VDSO_FOO 16\n")
        (self.headers / "utsversion.h").write_text("first\n")
        self.owners = ["arch/riscv/kernel/alternative.o", "arch/riscv/kernel/signal.o"]
        for name in self.owners:
            (self.out / name).parent.mkdir(parents=True, exist_ok=True)
            (self.minic / name).parent.mkdir(parents=True, exist_ok=True)
            (self.out / name).write_bytes(b"GCC")
            (self.minic / name).write_bytes(b"MINIC")
        self.selection = self.root / "selected.txt"
        self.selection.write_text("\n".join(self.owners) + "\n")
        self.golden = self.root / "golden"
        for name in self.owners:
            p = Path(name)
            dep = self.golden / p.parent / ("." + p.name + ".cmd")
            dep.parent.mkdir(parents=True, exist_ok=True)
            dep.write_text(f"savedcmd_{name} := riscv64-linux-gnu-gcc -c source.c\n"
                           f"source_{name} := source.c\n"
                           f"deps_{name} := include/linux/compiler.h include/generated/vdso-offsets.h\n")
        self.log = self.root / "link.log"
        self.log.write_text("  CC      arch/riscv/kernel/alternative.o\n"
                            "  CC      arch/riscv/kernel/signal.o\n")
        self.snap = self.root / "headers.json"
        mod.snapshot(self.out, self.snap)

    def tearDown(self):
        self.tmp.cleanup()

    def test_exact_gcc_recompile_with_stable_headers_is_repaired(self):
        # Kbuild recompiled two selected candidates; a second GNU link is
        # REQUIRED after this repair (the harness enforces that).
        repaired = mod.repair(self.out, self.minic, self.selection, self.log, self.snap)
        self.assertEqual(repaired, self.owners)
        for name in self.owners:
            self.assertEqual((self.out / name).read_bytes(), b"MINIC")
        self.assertEqual(mod.repair(self.out, self.minic, self.selection, self.log, self.snap), [])

    def test_changed_generated_header_must_repreprocess(self):
        (self.headers / "vdso-offsets.h").write_text("#define VDSO_FOO 32\n")
        with self.assertRaisesRegex(mod.GuardError, "MUST re-preprocess"):
            mod.repair(self.out, self.minic, self.selection, self.log, self.snap)
        for name in self.owners:
            self.assertEqual((self.out / name).read_bytes(), b"GCC")

    def test_changed_header_pinpoint_selected_dependent(self):
        (self.headers / "vdso-offsets.h").write_text("#define VDSO_FOO 32\\n")
        with self.assertRaisesRegex(mod.GuardError, "affected_count=2"):
            mod.repair(self.out, self.minic, self.selection, self.log, self.snap,
                       self.golden)

    def test_changed_header_without_selected_dependencies_safe(self):
        for name in self.owners:
            p = Path(name)
            dep = self.golden / p.parent / ("." + p.name + ".cmd")
            body = dep.read_text().replace(
                "include/generated/vdso-offsets.h", "include/generated/other.h")
            dep.write_text(body)
        (self.headers / "vdso-offsets.h").write_text("#define VDSO_FOO 32\\n")
        assert mod.repair(self.out, self.minic, self.selection, self.log,
                          self.snap, self.golden) == self.owners

    def test_missing_golden_dependency_closure_rejected(self):
        (self.headers / "vdso-offsets.h").write_text("#define VDSO_FOO 32\\n")
        (self.golden / "arch/riscv/kernel/.signal.o.cmd").unlink()
        with self.assertRaisesRegex(mod.GuardError, "missing pinned Kbuild"):
            mod.repair(self.out, self.minic, self.selection, self.log, self.snap,
                       self.golden)

    def test_unexplained_gcc_or_other_overwrite_is_rejected(self):
        self.log.write_text("  CC      unrelated/source.o\n")
        with self.assertRaisesRegex(mod.GuardError, "without Kbuild CC proof"):
            mod.repair(self.out, self.minic, self.selection, self.log, self.snap)

    def test_utsversion_regeneration_does_not_mask_other_headers(self):
        (self.headers / "utsversion.h").write_text("second\n")
        self.assertEqual(len(mod.repair(self.out, self.minic, self.selection, self.log, self.snap)), 2)

    def test_invalid_object_paths_are_rejected(self):
        self.selection.write_text("../etc/bad.o\n")
        with self.assertRaises(mod.GuardError):
            mod.repair(self.out, self.minic, self.selection, self.log, self.snap)

    def test_root_with_generated_subdirectories_is_handled(self):
        (self.out / "arch/riscv/include/generated/asm/auxvec.h").write_text("RV\n")
        d = mod.generated(self.out)
        self.assertIn("arch/riscv/include/generated/asm/auxvec.h", d)

    def test_workflow_candidate_cache_counter_accepts_real_names(self):
        # Regression for a typo in YAML: two literal backslashes in grep -E
        # matched no .o names and silently skipped caching all 2064 candidates.
        root = SCRIPT.parents[2]
        workflow = (root / ".github/workflows/linux-runtime-gcc-baseline.yml").read_text()
        m = re.search(r"count=\$\(grep -cE '([^']+)'", workflow)
        self.assertIsNotNone(m)
        with tempfile.TemporaryDirectory() as td:
            fixture = Path(td) / "full-objects.txt"
            fixture.write_text("# frozen universe\nlib/idr.o\nkernel/sched/core.o\nnot-a-target.s\n")
            v = subprocess.run(["grep", "-cE", m.group(1), str(fixture)],
                               text=True, capture_output=True, check=True)
        self.assertEqual(v.stdout.strip(), "2")


if __name__ == "__main__":
    unittest.main()
