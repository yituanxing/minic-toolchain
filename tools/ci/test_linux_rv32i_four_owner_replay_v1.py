#!/usr/bin/env python3
"""Offline mixed-epoch source, generated-header and full SHA census tests."""
from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("linux-rv32i-four-owner-replay-v1.py")
spec = importlib.util.spec_from_file_location("rv32four", SCRIPT)
mod = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(mod)


def sha(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()


class FourOwnerReplayTest(unittest.TestCase):
    def setUp(self):
        # /tmp can fill on builders retaining kernel evidence. Keep every
        # temporary fixture beneath the checkout, never the host /tmp.
        self.temp = tempfile.TemporaryDirectory(prefix="rv32-four-owner-",
                                                dir=Path(__file__).resolve().parents[2])
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.out = self.root / "out"
        self.merged = self.root / "merged"
        self.source = self.root / "linux-original"
        self.old_compiler = self.root / "old-minic"
        self.new_compiler = self.root / "new-minic"
        self.old_compiler.write_bytes(b"old producer")
        self.new_compiler.write_bytes(b"current compiler")
        self.owner_names = [mod.PRIMARY, *mod.GENERATED]
        self.owners = [*self.owner_names,
                       *(f"drivers/probe/test{index:04d}.o" for index in range(1737))]
        self.assertEqual(len(self.owners), 1741)
        self.old_sha = {}
        self.now_sha = {}
        for owner in self.owners:
            old_bytes = f"old:{owner}".encode()
            fresh_bytes = f"current:{owner}".encode() if owner in mod.CURRENT else old_bytes
            self.old_sha[owner] = sha(old_bytes)
            self.now_sha[owner] = sha(fresh_bytes)
            old_path = self.merged / "minic" / owner
            old_path.parent.mkdir(parents=True, exist_ok=True)
            old_path.write_bytes(fresh_bytes if owner == mod.PRIMARY else old_bytes)
            dst = self.out / owner
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(fresh_bytes)
        self.write_manifest("minic-manifest-before-replay.sha256", self.old_sha)
        self.write_manifest("minic-manifest.sha256", {
            name: self.now_sha[name] if name == mod.PRIMARY else self.old_sha[name]
            for name in self.owners})
        self.write_sha_trace("owner-replay-hashes.sha256", [mod.PRIMARY])
        self.write_sha_trace("header-replay-hashes.sha256", mod.GENERATED)
        (self.merged / "owner-replay-success.txt").write_text(mod.PRIMARY + "\n")
        (self.merged / "header-replay-success.txt").write_text("\n".join(mod.GENERATED) + "\n")
        (self.merged / "header-affected-objects.txt").write_text("\n".join(mod.GENERATED) + "\n")
        before = sha(b"old header")
        after = sha(b"new header")
        self.write_header_state("generated-before.json", before)
        self.write_header_state("generated-after-first-link.json", after)
        header = self.out / mod.HEADER
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_bytes(b"new header")
        image = self.out / "arch/riscv/boot/Image"
        image.parent.mkdir(parents=True, exist_ok=True)
        image.write_bytes(b"Image")
        for owner in self.owner_names:
            source = self.source / owner.removesuffix(".o")
            source = source.with_suffix(".c")
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text(f"int {Path(owner).stem}(void) {{ return 1; }}\n")
            p = Path(owner)
            cmd = self.merged / "gnu-cmd" / p.parent / f".{p.name}.cmd"
            cmd.parent.mkdir(parents=True, exist_ok=True)
            cmd.write_text(f"savedcmd_{owner} := riscv64-linux-gnu-gcc -c {source} -o {owner}\n")
            intermediate = self.out / owner.removesuffix(".o")
            intermediate.with_suffix(".minic-stage2.i").write_text(f"int i_{p.stem};\n")
            intermediate.with_suffix(".minic-stage2.s").write_text("nop\n")

    def write_manifest(self, name, table):
        (self.merged / name).write_text(
            "".join(f"{table[owner]}  minic/{owner}\n" for owner in self.owners))

    def write_sha_trace(self, name, subset):
        (self.merged / name).write_text(
            "".join(f"{self.now_sha[owner]}  {owner}\n" for owner in subset))

    def write_header_state(self, name, digest):
        (self.merged / name).parent.mkdir(parents=True, exist_ok=True)
        (self.merged / name).write_text(json.dumps({
            "schema": "minic-gnu-kbuild-regeneration-guard-v1",
            "headers": {mod.HEADER: digest, "include/generated/version.h": sha(b"fixed")},
        }))

    def test_proves_exact_four_current_and_1737_old(self):
        mod.attest(self.merged, self.out, self.source,
                   self.old_compiler, self.new_compiler)
        proof = (self.merged / "four-owner-attestation.txt").read_text()
        manifest = mod.records(self.merged / "four-owner-final-manifest.sha256")
        self.assertIn("old_producer_C_owners=1737\n", proof)
        self.assertIn("current_head_C_owners=4\n", proof)
        self.assertIn("current_minic_s_sha256=", proof)
        self.assertIn("original_source_sha256=", proof)
        self.assertEqual(manifest, self.now_sha)

    def test_rejects_any_other_generated_header(self):
        data = json.loads((self.merged / "generated-after-first-link.json").read_text())
        data["headers"]["include/generated/compile.h"] = sha(b"changed")
        (self.merged / "generated-after-first-link.json").write_text(json.dumps(data))
        with self.assertRaisesRegex(mod.ReplayError, "unexpected generated-header"):
            mod.gate(self.merged, self.out)

    def test_rejects_expanded_or_missing_consumers(self):
        p = self.merged / "header-affected-objects.txt"
        for names in ([*mod.GENERATED, "drivers/probe/test0001.o"], [mod.GENERATED[0]]):
            p.write_text("\n".join(names) + "\n")
            with self.assertRaisesRegex(mod.ReplayError, "consumer set"):
                mod.gate(self.merged, self.out)

    def test_rejects_any_legacy_object_postlink_drift(self):
        (self.out / "drivers/probe/test0003.o").write_bytes(b"GNU compiled a hidden owner")
        with self.assertRaisesRegex(mod.ReplayError, "postlink C owner SHA drift"):
            mod.attest(self.merged, self.out, self.source,
                       self.old_compiler, self.new_compiler)

    def test_rejects_misreported_current_header_owner(self):
        (self.merged / "header-replay-success.txt").write_text(
            "\n".join((mod.GENERATED[0], mod.GENERATED[0], mod.GENERATED[2])) + "\n")
        with self.assertRaisesRegex(mod.ReplayError, "successful compile trace"):
            mod.attest(self.merged, self.out, self.source,
                       self.old_compiler, self.new_compiler)

    def test_rejects_frozen_gnu_kbuild_source_drift(self):
        source = self.source / "arch/riscv/kernel/signal.c"
        source.write_bytes(b"changed original source")
        # A current frozen-source SHA is recorded (source is still canonical);
        # pointing the .cmd at a different C file must fail, however.
        cmd = self.merged / "gnu-cmd/arch/riscv/kernel/.signal.o.cmd"
        cmd.write_text(cmd.read_text().replace(str(source), str(self.source / "block/blk-mq.c")))
        with self.assertRaisesRegex(mod.ReplayError, "source diverged"):
            mod.attest(self.merged, self.out, self.source,
                       self.old_compiler, self.new_compiler)

    def test_rejects_missing_fresh_gnu_E_or_minic_S(self):
        p = self.out / "arch/riscv/kernel/vdso.minic-stage2.i"
        p.unlink()
        with self.assertRaisesRegex(mod.ReplayError, "missing, empty or symlinked evidence"):
            mod.attest(self.merged, self.out, self.source,
                       self.old_compiler, self.new_compiler)


if __name__ == "__main__":
    unittest.main()
