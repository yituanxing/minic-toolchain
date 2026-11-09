#!/usr/bin/env python3
"""Real GNU LD multiline syntax and fail-closed owner context tests."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("candidate", Path(__file__).with_name(
    "linux-three-isa-minic-candidate-v1.py"))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class LoongContextTest(unittest.TestCase):
    def test_real_log_and_adjacent_references(self):
        log = ("loongarch64-linux-gnu-ld: arch/loongarch/kernel/time.o: in function `calc_const_freq':\n"
               "(.text+0x3b4): undefined reference to `__cpucfg'\n"
               "loongarch64-linux-gnu-ld: (.text+0x488): undefined reference to `__cpucfg'\n"
               "loongarch64-linux-gnu-ld: drivers/nvme/host/pci.o: in function `.Lmini_loong_f11477_b1':\n"
               "(.init.text+0x60): undefined reference to `__compiletime_assert_449'\n")
        self.assertEqual(c.loongarch_linker_context_owners(log), [
            ("arch/loongarch/kernel/time.o", "__cpucfg"),
            ("arch/loongarch/kernel/time.o", "__cpucfg"),
            ("drivers/nvme/host/pci.o", "__compiletime_assert_449")])

    def test_no_basename_traversal_or_context_leak(self):
        for header in ("vmlinux.o", "pci.o", "../drivers/pci.o", "/drivers/pci.o"):
            log = (f"loongarch64-linux-gnu-ld: {header}: in function `f':\n"
                   "(.text+0x10): undefined reference to `bad'\n")
            self.assertEqual(c.loongarch_linker_context_owners(log), [])
        for intervening in ("warning: something else", "", "make: leaving directory",
                            "ld: other/owner.o: in function `f':"):
            log = ("loongarch64-linux-gnu-ld: drivers/nvme/host/pci.o: in function `f':\n"
                   f"{intervening}\n(.text+0x10): undefined reference to `bad'\n")
            self.assertEqual(c.loongarch_linker_context_owners(log), [])

    def test_context_console_not_mislabeled_elf_relocation(self):
        report = dict(verdict="LINK_FAILURE_C_OWNER_SAMPLES_COMPLETE", samples=[dict(
            owner="drivers/nvme/host/pci.o", input_sha256="a"*64, cmd_sha256="b"*64,
            owner_resolution=dict(method="authenticated_exact_loongarch_linker_context",
                                  linker_context_symbols=["__compiletime_assert_449"]))])
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            c.print_link_symbol_owner_diagnostics("loongarch64", dict(symbols=[
                dict(name="__compiletime_assert_449", references=1)]), report)
        entry = json.loads(output.getvalue().split(" ", 1)[1])
        self.assertEqual(entry["status"], "EXACT_AUTHENTICATED_LINKER_CONTEXT")
        self.assertEqual(entry["owners"][0]["object"], "drivers/nvme/host/pci.o")

    def test_capture_requires_manifest_and_staged_sha(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            work, out = root / "work", root / "output"
            (work / "evidence").mkdir(parents=True)
            name = "drivers/nvme/host/pci.o"
            source = work / "frozen/i/drivers/nvme/host/pci.i"
            cmd = work / "build/drivers/nvme/host/.pci.o.cmd"
            obj = out / "staged" / name
            for path in (source, cmd, obj):
                path.parent.mkdir(parents=True, exist_ok=True)
            source.write_text("original GNU preprocessed TU\n")
            cmd.write_text("original GNU command\n")
            header = bytearray(64)
            header[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<HH", header, 16, 1, 258)
            struct.pack_into("<I", header, 48, 1)
            obj.write_bytes(header)
            row = dict(object=name, input="i/drivers/nvme/host/pci.i", gnu_elf_abi=1,
                       sha256=c.sha(source), cmd_sha256=c.sha(cmd))
            result = dict(object=name, status="MINIC_S_GNU_AS_PASS", obj_sha256=c.sha(obj))
            (work / "evidence/discovery.json").write_text('{"gnu_target_objects":1}')
            (work / "evidence/gnu-target-objects.sha256").write_text("a"*64 + "  " + name + "\n")
            log = root / "link.log"
            log.write_text("loongarch64-linux-gnu-ld: " + name + ": in function `f':\n"
                           "(.init.text+0x60): undefined reference to `__compiletime_assert_449'\n")
            report = c.preserve_link_failed_c_owners(work, out, [row], [result], log,
                                                   target="loongarch64")
            self.assertEqual(report["samples"][0]["owner"], name)
            self.assertEqual(report["samples"][0]["owner_resolution"]["linker_context_symbols"],
                             ["__compiletime_assert_449"])
            # An exact-looking but nonmanifest path must never gain ownership.
            unknown = root / "unknown-output"
            unknown.mkdir()
            log.write_text(log.read_text().replace(name, "other/host/pci.o"))
            report = c.preserve_link_failed_c_owners(work, unknown, [row], [result], log,
                                                   target="loongarch64")
            self.assertEqual(report["samples"], [])
            log.write_text(log.read_text().replace("other/host/pci.o", name))
            obj.write_bytes(bytes(header) + b"changed")
            with self.assertRaises(c.Rejected):
                c.preserve_link_failed_c_owners(work, out, [row], [result], log,
                                               target="loongarch64")


if __name__ == "__main__":
    unittest.main()
