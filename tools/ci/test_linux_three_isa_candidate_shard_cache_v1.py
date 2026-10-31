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

    def fresh(self, resources=None):
        return c.identity("x86_64", self.root / "bin/minic", self.root / "as",
                          self.root / "config", self.root / "owners", self.rows,
                          self.root / "frozen", self.root / "build", 0,
                          external_resources=resources)

    def store(self):
        return c.store(self.root / "cache", self.expected, self.root / "staged", self.results)

    def test_round_trip_and_immutable(self):
        self.assertEqual(self.expected["helper_sha256"], c.LEGACY_REJECT_HELPER_SHA256)
        self.assertEqual(self.expected["external_asm_policy"], "reject")
        self.assertNotIn("external_asm_resources", self.expected)
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

    def genuine_gnu_incbin_fixture(self):
        resource = self.root / "build/kernel/config_data.gz"
        resource.write_bytes(b"actual-original-GNU-Kbuild-config-gzip-payload")
        src = self.root / "frozen/i/kernel/test.i"
        src.write_bytes(b'asm(".incbin \\"kernel/config_data.gz\\"");')
        asm = self.root / "staged/kernel/test.s"
        asm.write_bytes(b'.section .rodata\n.incbin "kernel/config_data.gz"\n')
        self.rows[0]["sha256"] = c.sha(src)
        self.results[0]["asm_sha256"] = c.sha(asm)
        resources = {"kernel/config_data.gz": c.sha(resource)}
        self.expected = self.fresh(resources)
        return resource, resources

    def test_genuine_gnu_incbin_sidecar_can_be_cached_and_reused(self):
        resource, resources = self.genuine_gnu_incbin_fixture()
        self.assertEqual(self.expected["external_asm_resources"], resources)
        self.assertEqual(self.expected["external_asm_policy"], "sha-attested-gnu-incbin-v1")
        self.store()
        self.assertTrue(c.verify(self.root / "cache", self.fresh(resources))[
            "compile_only_not_runtime_certified"])
        self.assertEqual(c.sha(resource), resources["kernel/config_data.gz"])

    def test_external_gnu_sidecar_drift_or_absence_invalidates_cache(self):
        resource, resources = self.genuine_gnu_incbin_fixture()
        self.store()
        resource.write_bytes(b"different-real-Kbuild-generated-bytes")
        with self.assertRaisesRegex(c.Rejected, "resource SHA changed"):
            self.fresh(resources)
        different = {"kernel/config_data.gz": c.sha(resource)}
        with self.assertRaisesRegex(c.Rejected, "cache identity mismatch"):
            c.verify(self.root / "cache", self.fresh(different))
        resource.unlink()
        with self.assertRaises(c.Rejected):
            self.fresh(resources)

    def test_untrusted_incbin_include_symlink_and_traversal_rejected(self):
        resource, resources = self.genuine_gnu_incbin_fixture()
        with self.assertRaisesRegex(c.Rejected, "unattested GNU .incbin"):
            c.no_external_assembly(self.root / "staged/kernel/test.s", {})
        with self.assertRaisesRegex(c.Rejected, "unsafe original GNU external resource"):
            self.fresh({"../kernel/config_data.gz": resources["kernel/config_data.gz"]})
        with self.assertRaisesRegex(c.Rejected, "external assembly resources"):
            asm = self.root / "staged/kernel/test.s"
            asm.write_bytes(b'.incbin "kernel/config_data.gz"\n.include "another.s"\n')
            self.results[0]["asm_sha256"] = c.sha(asm)
            self.store()
        resource.rename(resource.with_name("config_real.gz"))
        resource.symlink_to(resource.with_name("config_real.gz"))
        with self.assertRaisesRegex(c.Rejected, "symlink"):
            self.fresh(resources)

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
