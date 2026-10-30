#!/usr/bin/env python3
"""Fail-closed transport tests for the real 3 x 7 GNU Linux MiniC runtime."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace


def load(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


r = load("runtime_seven", "linux-three-isa-seven-runtime-v1.py")
c = r.candidate()


def write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def json_out(path: Path, data) -> None:
    write(path, (json.dumps(data, indent=2) + "\n").encode())


def elf(machine: int = 62, abi: int = 0) -> bytes:
    data = bytearray(64)
    data[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<HH", data, 16, 1, machine)
    struct.pack_into("<I", data, 48, abi)
    return bytes(data)


class SevenWorkerStrictTransportTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="minic-seven-runtime-test-")
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name)
        self.input = self.dir / "input"
        self.incoming = self.dir / "incoming"
        self.output = self.dir / "out"
        self.compiler = self.dir / "minic"
        write(self.compiler, b"mini-compiler-pair-stub")
        write(self.input / "build/.config", b"CONFIG_SYNTHETIC=y\n")
        write(self.input / "evidence/full-c-owners.txt", b"synthetic original owners\n")
        self.rows = []
        for index in range(210):
            name = f"kernel/owner_{index}.o"
            inp = f"i/kernel/owner_{index}.i"
            source = (f"int foo_{index}(void){{return {index};}}\n").encode()
            cmd = f"cmd_kernel/owner_{index}.o := gcc -c kernel/owner_{index}.c\n".encode()
            write(self.input / "frozen" / inp, source)
            write(self.input / "build/kernel" / f".owner_{index}.o.cmd", cmd)
            self.rows.append(dict(index=index, shard=index % 7, object=name,
                                  input=inp, sha256=r.file_sha(self.input / "frozen" / inp),
                                  cmd_sha256=r.file_sha(self.input / "build/kernel" / f".owner_{index}.o.cmd")))
        json_out(self.input / "frozen/manifest.json", {"rows": self.rows})
        self.original_identity = dict(
            schema="minic-three-isa-seven-input-v1", target="x86_64", shard=2,
            total_c_owners=210, shard_c_owners=30,
            frozen_manifest_sha256=r.file_sha(self.input / "frozen/manifest.json"),
            original_config_sha256=r.file_sha(self.input / "build/.config"),
            original_owner_list_sha256=r.file_sha(self.input / "evidence/full-c-owners.txt"),
            original_gnu_image_sha256="a" * 64,
            gnu_c_owner_manifest_sha256="b" * 64,
            gnu_receipt_sha256="c" * 64,
            compiler_driver_sha256="d" * 64,
            compiler_cc_sha256="e" * 64,
            external_build_resources={})
        json_out(self.input / "identity.json", self.original_identity)

    def test_exact_input_sha_and_membership(self):
        rows, identity = r.verify_input(self.input, "x86_64", 2, c)
        self.assertEqual(len(rows), 30)
        self.assertEqual(identity["target"], "x86_64")
        with self.assertRaisesRegex(ValueError, "foreign target"):
            r.verify_input(self.input, "aarch64", 2, c)
        with self.assertRaisesRegex(ValueError, "foreign target"):
            r.verify_input(self.input, "x86_64", 3, c)
        path = self.input / "frozen/i/kernel/owner_2.i"
        write(path, b"int gcc_fallback(void){return 0;}\n")
        with self.assertRaisesRegex(ValueError, "SHA changed"):
            r.verify_input(self.input, "x86_64", 2, c)

    def test_corrupt_command_external_resource_and_manifest(self):
        cmd = self.input / "build/kernel/.owner_2.o.cmd"
        write(cmd, b"cmd forged!\n")
        with self.assertRaisesRegex(ValueError, "SHA changed"):
            r.verify_input(self.input, "x86_64", 2, c)
        write(cmd, f"cmd_kernel/owner_2.o := gcc -c kernel/owner_2.c\n".encode())
        identity = dict(self.original_identity, external_build_resources={"kernel/config_data.gz": "e" * 64})
        json_out(self.input / "identity.json", identity)
        with self.assertRaisesRegex((ValueError, OSError), "missing/unsafe"):
            r.verify_input(self.input, "x86_64", 2, c)

    def populate_objects(self):
        rows = self.rows
        for shard in range(7):
            selected = rows[shard::7]
            folder = self.incoming / f"minic-linux-seven-objects-x86_64-{shard}"
            results = []
            for row in selected:
                obj = folder / "objects" / row["object"]
                write(obj, elf())
                results.append(dict(object=row["object"], input_sha256=row["sha256"],
                                    status="MINIC_S_GNU_AS_PASS", asm_sha256="f" * 64,
                                    obj_sha256=r.file_sha(obj)))
            json_out(folder / "results.json", results)
            json_out(folder / "identity.json", {
                **self.original_identity, "schema": "minic-three-isa-seven-objects-v1",
                "shard": shard, "shard_c_owners": len(selected),
                "gnu_gcc_binary_sha256": "7" * 64, "gnu_as_binary_sha256": "8" * 64,
                "compiler_sha256": "d" * 64})

    def collect(self):
        a = SimpleNamespace(incoming=self.incoming, output=self.output,
                            target="x86_64", minic=self.compiler)
        original_pair = c.compiler_pair
        original_gnu_as_identity = r.gnu_as_identity
        c.compiler_pair = lambda *args, **kwargs: {
            "compiler_sha256": "d" * 64,
            "compiler_driver_sha256": "d" * 64,
            "compiler_cc_sha256": "e" * 64}
        r.gnu_as_identity = lambda *args, **kwargs: {
            "gnu_gcc_binary_sha256": "7" * 64,
            "gnu_as_binary_sha256": "8" * 64}
        try:
            return r.collect(a, c, self.rows, self.original_identity)
        finally:
            c.compiler_pair = original_pair
            r.gnu_as_identity = original_gnu_as_identity

    def test_exact_complete_seven_shards_and_missing_shard(self):
        self.populate_objects()
        results = self.collect()
        self.assertEqual(len(results), 210)
        self.assertEqual([row["object"] for row in results],
                         [row["object"] for row in self.rows])
        self.assertEqual(len(list((self.output / "staged").rglob("*.o"))), 210)
        victim = self.incoming / "minic-linux-seven-objects-x86_64-6/identity.json"
        victim.unlink()
        with self.assertRaisesRegex(ValueError, "missing or unsafe"):
            self.collect()

    def test_wrong_target_wrong_owner_gnu_c_fallback_and_tampered_o(self):
        self.populate_objects()
        root = self.incoming / "minic-linux-seven-objects-x86_64-0"
        receipt = r.json_file(root / "identity.json")
        receipt["target"] = "aarch64"
        json_out(root / "identity.json", receipt)
        with self.assertRaisesRegex(ValueError, "foreign or incomplete"):
            self.collect()
        receipt["target"] = "x86_64"
        json_out(root / "identity.json", receipt)
        rows = r.json_file(root / "results.json")
        rows[0]["object"] = rows[1]["object"]
        json_out(root / "results.json", rows)
        with self.assertRaisesRegex(ValueError, "incomplete or duplicate"):
            self.collect()
        rows[0]["object"] = self.rows[0]["object"]
        rows[0]["status"] = "GNU_GCC_C_FALLBACK"
        json_out(root / "results.json", rows)
        with self.assertRaisesRegex(ValueError, "incomplete or duplicate"):
            self.collect()
        rows[0]["status"] = "MINIC_S_GNU_AS_PASS"
        json_out(root / "results.json", rows)
        obj = root / "objects/kernel/owner_0.o"
        write(obj, elf(machine=183))
        with self.assertRaisesRegex(ValueError, "SHA mismatch"):
            self.collect()

    def test_machine_and_loongarch_abi(self):
        path = self.dir / "object.o"
        write(path, elf(machine=258, abi=1))
        c.elf_machine(path, 258)
        c.validate_original_abi(path, "loongarch64", {"gnu_elf_abi": 1, "object": "kernel/probe.o"})
        write(path, elf(machine=258, abi=2))
        with self.assertRaisesRegex(ValueError, "ABI mismatch"):
            c.validate_original_abi(path, "loongarch64", {"gnu_elf_abi": 1, "object": "kernel/probe.o"})
        with self.assertRaisesRegex(ValueError, "not a relocatable target ELF64"):
            c.elf_machine(path, 183)

    def test_trusted_gnu_driver_symlink_resolves_without_relaxing_inputs(self):
        actual = self.dir / "gcc-real"
        alias = self.dir / "gcc"
        write(actual, b"installed-gnu-tool")
        alias.symlink_to(actual)
        with mock.patch.object(r.shutil, "which", return_value=str(alias)):
            self.assertEqual(r.trusted_gnu_driver_binary("gcc"), actual)
        with mock.patch.object(r.shutil, "which", return_value=None):
            with self.assertRaisesRegex(ValueError, "GNU assembler compiler missing"):
                r.trusted_gnu_driver_binary("gcc")

    def test_gnu_as_identity_resolves_both_trusted_gnu_symlinks(self):
        gcc_real = self.dir / "gcc-real"
        as_real = self.dir / "as-real"
        gcc_alias = self.dir / "gcc"
        as_alias = self.dir / "as"
        write(gcc_real, b"real-gcc-binary")
        write(as_real, b"real-as-binary")
        gcc_alias.symlink_to(gcc_real)
        as_alias.symlink_to(as_real)
        with mock.patch.object(r.shutil, "which", side_effect=[str(gcc_alias), str(as_alias)]):
            self.assertEqual(r.gnu_as_identity(c, "x86_64"), {
                "gnu_gcc_binary_sha256": r.file_sha(gcc_real),
                "gnu_as_binary_sha256": r.file_sha(as_real)})
        with self.assertRaisesRegex(ValueError, "missing/unsafe"):
            c.required(gcc_alias)

    def test_candidate_cache_identity_binds_real_assembler_sha(self):
        assembler = self.dir / "real-gnu-as"
        write(assembler, b"installed-gnu-as-version-1")
        captured = []
        fake_helper = SimpleNamespace(identity=lambda *args: captured.append(args) or {"ok": True})
        args = SimpleNamespace(input=self.input, target="x86_64", shard=2, minic=self.compiler)
        with mock.patch.object(r, "trusted_gnu_driver_binary", return_value=assembler), \
                mock.patch.object(c, "module", return_value=fake_helper):
            helper, identity = r.cache_identity(args, c)
        self.assertIs(helper, fake_helper)
        self.assertEqual(identity, {"ok": True})
        self.assertEqual(captured[0][2], assembler)

    def test_producer_shard_receipt_binds_compiler_and_gnu_as(self):
        output = self.dir / "producer"
        staged = output / "staged/kernel/owner_2.o"
        write(staged, elf())
        result = {"object": "kernel/owner_2.o", "input_sha256": self.rows[2]["sha256"],
                  "status": "MINIC_S_GNU_AS_PASS", "obj_sha256": r.file_sha(staged),
                  "asm_sha256": "f" * 64}
        compiler_pair = {"compiler_sha256": "a" * 64,
                         "compiler_driver_sha256": "b" * 64,
                         "compiler_cc_sha256": "c" * 64}
        gnu_pair = {"gnu_gcc_binary_sha256": "d" * 64,
                    "gnu_as_binary_sha256": "e" * 64}
        args = SimpleNamespace(output=output, minic=self.compiler, target="x86_64")
        with mock.patch.object(c, "compiler_pair", return_value=compiler_pair), \
                mock.patch.object(r, "gnu_as_identity", return_value=gnu_pair):
            r.package_shard(args, c, [result], self.original_identity)
        receipt = r.json_file(output / "bundle/identity.json")
        for field, value in {**compiler_pair, **gnu_pair}.items():
            self.assertEqual(receipt[field], value)
        self.assertEqual(r.file_sha(output / "bundle/objects/kernel/owner_2.o"),
                         result["obj_sha256"])


if __name__ == "__main__":
    unittest.main()
