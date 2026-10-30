#!/usr/bin/env python3
"""Synthetic cache integrity tests; not Linux/QEMU certification."""
import copy
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("cache", Path(__file__).with_name(
    "linux-three-isa-candidate-shard-cache-v1.py"))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class CacheTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name, data in {"bin/minic": b"driver", "bin/minic-cc": b"compiler",
                           "as": b"GNU assembler", "config": b"CONFIG=y",
                           "owners": b"kernel/test.o\n", "frozen/i/kernel/test.i": b"int f;",
                           "build/kernel/.test.o.cmd": b"savedcmd := gcc -c test.c",
                           "staged/kernel/test.s": b".text\n"}.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        hdr = bytearray(64)
        hdr[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<HH", hdr, 16, 1, 62)
        (self.root / "staged/kernel/test.o").write_bytes(hdr)
        self.rows = [dict(index=0, shard=0, object="kernel/test.o", input="i/kernel/test.i",
                          sha256=c.sha(self.root / "frozen/i/kernel/test.i"),
                          cmd_sha256=c.sha(self.root / "build/kernel/.test.o.cmd"),
                          minic_flags=["-fsigned-char"], gnu_as_flags=["-m64"])]
        self.expected = self.fresh()
        self.results = [dict(object="kernel/test.o", status="MINIC_S_GNU_AS_PASS",
                             asm_sha256=c.sha(self.root / "staged/kernel/test.s"),
                             obj_sha256=c.sha(self.root / "staged/kernel/test.o"))]

    def fresh(self):
        return c.identity("x86_64", self.root / "bin/minic", self.root / "as",
                          self.root / "config", self.root / "owners", self.rows,
                          self.root / "frozen", self.root / "build", 0)

    def store(self):
        return c.store(self.root / "cache", self.expected, self.root / "staged", self.results)

    def test_round_trip_and_immutable(self):
        self.store()
        self.assertTrue(c.verify(self.root / "cache", self.fresh())["compile_only_not_runtime_certified"])
        with self.assertRaises(c.Rejected):
            self.store()

    def test_each_identity_dimension_misses(self):
        self.store()
        for name in ("bin/minic", "bin/minic-cc", "as", "config", "owners"):
            p = self.root / name
            original = p.read_bytes()
            p.write_bytes(original + b"changed")
            with self.assertRaises(c.Rejected):
                c.verify(self.root / "cache", self.fresh())
            p.write_bytes(original)
        for field, value in (("target", "aarch64"), ("shard", 1)):
            altered = copy.deepcopy(self.expected)
            altered[field] = value
            with self.assertRaises(c.Rejected):
                c.verify(self.root / "cache", altered)
        self.rows[0]["minic_flags"].append("-fshort-wchar")
        with self.assertRaises(c.Rejected):
            c.verify(self.root / "cache", self.fresh())

    def test_tu_and_command_tampering(self):
        for name in ("frozen/i/kernel/test.i", "build/kernel/.test.o.cmd"):
            p = self.root / name
            original = p.read_bytes()
            p.write_bytes(original + b"changed")
            with self.assertRaises(c.Rejected):
                self.fresh()
            p.write_bytes(original)

    def test_corrupt_artifact_and_manifest(self):
        self.store()
        obj = self.root / "cache/staged/kernel/test.o"
        original = obj.read_bytes()
        obj.write_bytes(original + b"corrupt")
        with self.assertRaises(c.Rejected):
            c.verify(self.root / "cache", self.expected)
        obj.write_bytes(original)
        (self.root / "cache/manifest.json").write_text("{}")
        with self.assertRaises(c.Rejected):
            c.verify(self.root / "cache", self.expected)

    def test_hidden_resources_cold(self):
        p = self.root / "frozen/i/kernel/test.i"
        p.write_bytes(b'asm(".incbin \\"kernel/config_data.gz\\"");')
        self.rows[0]["sha256"] = c.sha(p)
        with self.assertRaises(c.Rejected):
            self.fresh()
        (self.root / "staged/kernel/test.s").write_text('.include "anything"\n')
        with self.assertRaises(c.Rejected):
            self.store()

    def test_wrong_machine_and_failed_compile(self):
        self.results[0]["status"] = "MINIC_S_FAIL"
        with self.assertRaises(c.Rejected):
            self.store()
        with self.assertRaises(c.Rejected):
            c.check_elf(self.root / "staged/kernel/test.o", dict(machine=183, target="aarch64"), {})
        with self.assertRaises(c.Rejected):
            c.check_elf(self.root / "staged/kernel/test.o", dict(machine=62, target="loongarch64"), {})

    def test_extra_file_and_symlink(self):
        self.store()
        extra = self.root / "cache/extra"
        extra.write_text("unattested")
        with self.assertRaises(c.Rejected):
            c.verify(self.root / "cache", self.expected)
        extra.unlink()
        extra.symlink_to(self.root / "config")
        with self.assertRaises(c.Rejected):
            c.verify(self.root / "cache", self.expected)


if __name__ == "__main__":
    unittest.main()
