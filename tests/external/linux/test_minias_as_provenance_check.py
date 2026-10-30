#!/usr/bin/env python3
"""Small, toolchain-free Kbuild owner/AS trace regressions."""

from pathlib import Path
import importlib.util
import tempfile
import unittest


MODULE = Path(__file__).with_name("minias_as_provenance_check.py")
SPEC = importlib.util.spec_from_file_location("minias_as_provenance_check", MODULE)
assert SPEC and SPEC.loader
PROVENANCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROVENANCE)


class ProvenanceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name) / "build"
        self.out.mkdir()
        self.trace = self.out.parent / "trace.log"

    def object(self, owner: str, command: str) -> None:
        path = self.out / owner
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"ELF-STUB")
        (path.parent / ("." + path.name + ".cmd")).write_text(
            f"savedcmd_{owner} := {command}\n"
        )

    def common(self) -> None:
        self.object("arch/arm64/kernel/entry.o",
                    "aarch64-linux-gnu-gcc -pipe -c -o "
                    "arch/arm64/kernel/entry.o entry.S")
        self.object("mm/page.o",
                    "aarch64-linux-gnu-as -o mm/page.o page.s")
        self.object("lib/composite.o",
                    "aarch64-linux-gnu-ld -r -o lib/composite.o part.o")
        self.trace.write_text(
            f"assemble cwd={self.out} input=entry.s output=arch/arm64/kernel/entry.o\n"
            f"pass cwd={self.out} input=entry.s output=arch/arm64/kernel/entry.o\n"
            f"assemble cwd={self.out} input=page.s output=mm/page.o\n"
            f"pass cwd={self.out} input=page.s output=mm/page.o\n"
            "delegate-query --version\n"
        )

    def verify(self, minimum: int = 2):
        return PROVENANCE.verify(self.out, self.trace, "aarch64-linux-gnu-gcc",
                                 "aarch64-linux-gnu-as", minimum, "aarch64")

    def test_compiler_assembler_and_linker_separation(self) -> None:
        self.common()
        expected, passed, counters = self.verify()
        self.assertEqual((expected, passed, counters["queries"]), (2, 2, 1))

    def test_missing_owner_fails_closed(self) -> None:
        self.common()
        self.trace.write_text(self.trace.read_text().replace(
            f"pass cwd={self.out} input=page.s output=mm/page.o\n", ""))
        with self.assertRaisesRegex(ValueError, "wrapper trace incomplete"):
            self.verify()

    def test_complete_trace_but_untracked_owner_fails(self) -> None:
        self.common()
        self.object("kernel/third.o",
                    "aarch64-linux-gnu-gcc -c -o kernel/third.o third.c")
        with self.assertRaisesRegex(ValueError, "bypassed MiniAS"):
            self.verify()

    def test_empty_object_fails(self) -> None:
        self.common()
        (self.out / "mm/page.o").write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "missing/empty"):
            self.verify()

    def test_duplicate_production_pass_fails(self) -> None:
        self.common()
        lines = self.trace.read_text().splitlines(keepends=True)
        self.trace.write_text("".join(lines + [lines[0], lines[1]]))
        with self.assertRaisesRegex(ValueError, "assembled multiple times"):
            self.verify()

    def test_duplicate_feature_probe_is_allowed(self) -> None:
        self.common()
        probe = "/tmp/gcc-kbuild-capability-probe.o"
        self.trace.write_text(self.trace.read_text() +
                              f"assemble cwd={self.out} input=p.s output={probe}\n"
                              f"pass cwd={self.out} input=p.s output={probe}\n"
                              f"assemble cwd={self.out} input=p.s output={probe}\n"
                              f"fail status=1 cwd={self.out} input=p.s output={probe}\n")
        expected, passed, counters = self.verify()
        self.assertEqual((expected, passed, counters["fail"]), (2, 3, 1))

    def test_query_only_is_not_production_coverage(self) -> None:
        self.trace.write_text("delegate-query --version\n")
        with self.assertRaisesRegex(ValueError, "insufficient target Kbuild"):
            self.verify()

    def test_kconfig_probe_only_is_not_production_coverage(self) -> None:
        self.common()
        self.trace.write_text(self.trace.read_text() +
                              "delegate-kconfig-probe cwd=/tmp input=/tmp/ccfoo.s output=/dev/null\n")
        expected, passed, counters = self.verify()
        self.assertEqual((expected, passed, counters["queries"]), (2, 2, 2))

    def test_relative_cwd_output(self) -> None:
        self.common()
        nested = self.out / "mm"
        self.trace.write_text(self.trace.read_text().replace(
            f"cwd={self.out} input=page.s output=mm/page.o",
            f"cwd={nested} input=page.s output=page.o"))
        expected, _, _ = self.verify()
        self.assertEqual(expected, 2)

    def test_multiline_savedcmd_compat(self) -> None:
        self.common()
        cmd = self.out / "mm/.page.o.cmd"
        cmd.write_text("cmd_mm/page.o := aarch64-linux-gnu-as \\\n+   -o mm/page.o page.s\n")
        expected, _, _ = self.verify()
        self.assertEqual(expected, 2)

    def test_loongarch_pipe_stdin(self) -> None:
        self.object("arch/loongarch/kernel/foo.o",
                    "loongarch64-linux-gnu-gcc-13 -pipe -c -o "
                    "arch/loongarch/kernel/foo.o foo.c")
        self.trace.write_text(
            f"assemble cwd={self.out} input=/tmp/minias-loong-kbuild.001.s "
            "output=arch/loongarch/kernel/foo.o\n"
            f"pass cwd={self.out} input=/tmp/minias-loong-kbuild.001.s "
            "output=arch/loongarch/kernel/foo.o\n"
        )
        expected, _, _ = PROVENANCE.verify(
            self.out, self.trace, "loongarch64-linux-gnu-gcc-13",
            "loongarch64-linux-gnu-as", 1, "loongarch64")
        self.assertEqual(expected, 1)

    def test_native_x86_hostcc_not_counted(self) -> None:
        self.object("scripts/basic/fixdep.o",
                    "gcc -c -o scripts/basic/fixdep.o fixdep.c")
        self.object("arch/x86/boot/tools/build.o",
                    "gcc -c -o arch/x86/boot/tools/build.o build.c")
        self.object("arch/x86/kernel/head.o",
                    "gcc -B/tmp/asm-bin/ -c -o arch/x86/kernel/head.o head.S")
        self.trace.write_text(
            f"assemble cwd={self.out} input=head.s output=arch/x86/kernel/head.o\n"
            f"pass cwd={self.out} input=head.s output=arch/x86/kernel/head.o\n"
        )
        expected, passed, _ = PROVENANCE.verify(self.out, self.trace,
                                                "gcc", "as", 1, "x86_64")
        self.assertEqual((expected, passed), (1, 1))

    def test_native_x86_target_cc_without_wrapper_is_detected(self) -> None:
        self.object("kernel/workqueue.o",
                    "gcc -c -o kernel/workqueue.o workqueue.c")
        self.trace.write_text("delegate-query --version\n")
        with self.assertRaisesRegex(ValueError, "bypassed MiniAS"):
            PROVENANCE.verify(self.out, self.trace, "gcc", "as", 1, "x86_64")

    def test_native_x86_prefixed_direct_as_is_counted(self) -> None:
        self.object("arch/x86/boot/setup.o",
                    "x86_64-linux-gnu-as --32 -o arch/x86/boot/setup.o setup.s")
        self.trace.write_text(
            f"assemble cwd={self.out} input=setup.s output=arch/x86/boot/setup.o\n"
            f"pass cwd={self.out} input=setup.s output=arch/x86/boot/setup.o\n"
        )
        expected, _, _ = PROVENANCE.verify(self.out, self.trace,
                                            "gcc", "as", 1, "x86_64")
        self.assertEqual(expected, 1)


if __name__ == "__main__":
    unittest.main()
