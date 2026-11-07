#!/usr/bin/env python3
"""Offline checks for the RV32 exact-epoch, seven-shard partial-owner cache."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "rv32_seven_partial_under_test", Path(__file__).with_name("linux-rv32i-shard-partial-v1.py"))
assert spec is not None and spec.loader is not None
rv32 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rv32)


def elf32_riscv(payload: bytes = b"ORIGINAL", flags: int = 0) -> bytes:
    header = bytearray(52)
    header[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<H", header, 16, 1)
    struct.pack_into("<H", header, 18, 243)
    struct.pack_into("<I", header, 36, flags)
    return bytes(header) + payload


class Args:
    pass


class ResumeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.out = self.root / "out"
        self.old = self.root / "old"
        self.now = self.root / "now"
        self.owner = "init/main.o"
        self.original = self.root / "linux-src"
        self.source = self.original / "init/main.c"
        self.source.parent.mkdir(parents=True)
        self.source.write_text("int main(void) { return 3; }\n")
        (self.out / "init").mkdir(parents=True)
        self.cmd = self.out / "init/.main.o.cmd"
        self.cmd.write_text("savedcmd_init/main.o := riscv64-linux-gnu-gcc -O2 -march=rv32ima "
                            f"-mabi=ilp32 -c {self.source} -o init/main.o\n"
                            "deps_init/main.o := init/main.c include/generated/autoconf.h\n")
        (self.out / self.owner).write_bytes(elf32_riscv())
        self.assembly = self.out / "init/main.minic-stage2.s"
        self.preprocessed = self.out / "init/main.minic-stage2.i"
        self.assembly.write_text(".text\n")
        self.preprocessed.write_text("int main(void){return 3;}\n")
        self.a = Args()
        self.a.partial = self.old
        self.a.out = self.out
        self.a.owner = self.owner
        self.a.object = self.out / self.owner
        self.a.assembly = self.assembly
        self.a.preprocessed = self.preprocessed
        self.a.resume = self.old
        self.a.root = self.root
        self.minic = self.root / "minic-cc"
        self.minic.write_text("current MiniC binary\n")
        self.a.minic = self.minic
        self.identity = {
            "schema": rv32.SCHEMA,
            "rows": [{"object": self.owner, "cmd_sha256": rv32.sha(self.cmd),
                      "source_sha256": rv32.sha(self.source)}],
            "generated_headers": {"include/generated/autoconf.h": "a" * 64},
            "current_minic_compiler_sha256": rv32.sha(self.minic),
            "current_compiler_source_profile_sha256": "c" * 64,
            "gnu_gcc_binary_sha256": "d" * 64,
            "gnu_as_binary_sha256": "e" * 64,
        }
        rv32.seven.atomic_json(self.old / "checkpoint/identity.json", self.identity)

    def record_success(self):
        rv32.capture(self.a)
        proof = rv32.seven.json_file(
            self.old / "checkpoint/owners" / (self.owner + ".json"))
        self.assertEqual(proof["status"], rv32.STATUS)
        self.assertEqual(proof["input_sha256"], rv32.sha(self.preprocessed))
        self.assertEqual(proof["asm_sha256"], rv32.sha(self.assembly))
        self.assertEqual(proof["obj_sha256"], rv32.sha(self.out / self.owner))
        return proof

    def prepare_resume(self):
        receipt = self.record_success()
        self.a.partial = self.now
        rv32.seven.atomic_json(self.now / "checkpoint/identity.json", self.identity)
        (self.out / self.owner).write_bytes(elf32_riscv(b"GOLDEN"))
        return receipt

    def freeze_dynamic_header_epoch(self, affected: bool):
        """Seal original GNU dependency syntax before any wrapper mutation."""
        self.cmd.write_text(
            "savedcmd_init/main.o := riscv64-linux-gnu-gcc -O2 -march=rv32ima "
            f"-mabi=ilp32 -c {self.source} -o init/main.o\n"
            "deps_init/main.o := init/main.c include/generated/autoconf.h"
            + (" include/generated/vdso-offsets.h" if affected else "") + "\n")
        self.identity["rows"][0]["cmd_sha256"] = rv32.sha(self.cmd)
        self.identity["generated_headers"] = {
            "include/generated/autoconf.h": "a" * 64,
            "include/generated/vdso-offsets.h": "b" * 64,
        }
        rv32.seven.atomic_json(self.old / "checkpoint/identity.json", self.identity)
        pinned = self.old / "checkpoint/pinned-gnu-cmd/init"
        pinned.mkdir(parents=True)
        shutil.copyfile(self.cmd, pinned / ".main.o.cmd")
        (self.old / "checkpoint/frozen-selected.txt").write_text(self.owner + "\n")
        rv32.seven.atomic_json(self.old / "checkpoint/frozen-header-snapshot.json",
                              {"schema": rv32.regen.SCHEMA,
                               "headers": self.identity["generated_headers"]})
        return dict(self.identity["generated_headers"],
                    **{"include/generated/vdso-offsets.h": "d" * 64})

    def test_real_vdso_header_change_without_dependent_c_is_sha_sealed(self):
        after = self.freeze_dynamic_header_epoch(affected=False)
        self.record_success()
        with patch.object(rv32.regen, "generated", return_value=after), \
             patch.object(rv32, "source_profile", return_value=self.identity["current_compiler_source_profile_sha256"]), \
             patch.object(rv32.regen, "replay_affected") as replay:
            rv32.reconcile(self.a)
            rv32.verify(self.a)
        replay.assert_not_called()
        self.assertEqual(rv32.seven.json_file(
            self.old / "checkpoint/post-generated-header-epoch.json")["changed"],
                         ["include/generated/vdso-offsets.h"])

    def test_vdso_dependent_c_must_replay_current_gnu_e_before_sha_seal(self):
        after = self.freeze_dynamic_header_epoch(affected=True)
        self.record_success()
        with patch.object(rv32.regen, "generated", return_value=after), \
             patch.object(rv32, "source_profile", return_value=self.identity["current_compiler_source_profile_sha256"]), \
             patch.object(rv32.regen, "replay_affected") as replay:
            with patch.object(rv32, "preprocessed_sha", return_value="0" * 64):
                rv32.reconcile(self.a)
                with self.assertRaisesRegex(ValueError, "dynamic GNU header owner not replayed"):
                    rv32.verify(self.a)
            replay.assert_called_once()
            # Simulate the wrapper's actual compiler path producing new .i
            # and atomic .o/.s receipt on changed generated-header input.
            self.preprocessed.write_text("changed GNU-E under regenerated offsets\n")
            rv32.capture(self.a)
            with patch.object(rv32, "preprocessed_sha", return_value=rv32.sha(self.preprocessed)):
                rv32.verify(self.a)

    def test_unrelated_generated_header_change_remains_hard_failure(self):
        after = self.freeze_dynamic_header_epoch(affected=False)
        after["include/generated/autoconf.h"] = "f" * 64
        self.record_success()
        with patch.object(rv32.regen, "generated", return_value=after), \
             patch.object(rv32, "source_profile", return_value=self.identity["current_compiler_source_profile_sha256"]):
            with self.assertRaisesRegex(rv32.regen.GuardError, "unsupported generated-header changes"):
                rv32.reconcile(self.a)

    def test_resume_skips_only_dynamic_header_dependent_mismatched_gnu_e(self):
        self.freeze_dynamic_header_epoch(affected=True)
        self.prepare_resume()
        # `prepare_resume` creates a new current shard: its current frozen
        # original `.cmd` must be present to authenticate the dependency edge.
        pinned = self.now / "checkpoint/pinned-gnu-cmd/init"
        pinned.mkdir(parents=True)
        shutil.copyfile(self.cmd, pinned / ".main.o.cmd")
        (self.now / "checkpoint/frozen-selected.txt").write_text(self.owner + "\n")
        rv32.seven.atomic_json(self.now / "checkpoint/frozen-header-snapshot.json",
                              {"schema": rv32.regen.SCHEMA,
                               "headers": self.identity["generated_headers"]})
        with patch.object(rv32, "preprocessed_sha", return_value="f" * 64):
            rv32.restore(self.a)
        self.assertEqual((self.now / "checkpoint/resumed-owners.txt").read_text(), "")
        self.assertEqual((self.out / self.owner).read_bytes(), elf32_riscv(b"GOLDEN"))

    def test_final_sha_trace_uses_one_authenticated_object(self):
        self.record_success()
        self.a.success_trace = self.root / "success.txt"
        self.a.digest_trace = self.root / "hashes.txt"
        rv32.publish_traces(self.a)
        self.assertEqual(self.a.success_trace.read_text(), self.owner + "\n")
        self.assertEqual(self.a.digest_trace.read_text(),
                         rv32.sha(self.out / self.owner) + "  " + self.owner + "\n")

    def test_compile_success_is_staged_before_atomic_owner_receipt(self):
        proof = self.record_success()
        staged = self.old / "staged" / self.owner
        self.assertEqual(rv32.sha(staged), proof["obj_sha256"])
        self.assertEqual(rv32.sha(staged.with_suffix(".s")), proof["asm_sha256"])
        with patch.object(rv32.regen, "generated", return_value=self.identity["generated_headers"]), \
             patch.object(rv32, "source_profile", return_value=self.identity["current_compiler_source_profile_sha256"]):
            rv32.verify(self.a)

    def test_reuse_requires_authentic_gnu_e_and_restores_exact_mini_c_object(self):
        proof = self.prepare_resume()
        with patch.object(rv32, "preprocessed_sha", return_value=proof["input_sha256"]):
            rv32.restore(self.a)
        self.assertEqual(rv32.sha(self.out / self.owner), proof["obj_sha256"])
        self.assertEqual((self.now / "checkpoint/resumed-owners.txt").read_text(),
                         self.owner + "\n")
        with patch.object(rv32.regen, "generated", return_value=self.identity["generated_headers"]), \
             patch.object(rv32, "source_profile", return_value=self.identity["current_compiler_source_profile_sha256"]):
            rv32.verify(self.a)

    def test_tampered_saved_object_rejected_before_restoration(self):
        self.prepare_resume()
        staged = self.old / "staged" / self.owner
        staged.write_bytes(elf32_riscv(b"TAMPER"))
        with patch.object(rv32, "preprocessed_sha") as pp:
            with self.assertRaisesRegex(ValueError, "tampered old successful"):
                rv32.restore(self.a)
        pp.assert_not_called()
        self.assertEqual((self.out / self.owner).read_bytes(), elf32_riscv(b"GOLDEN"))

    def test_changed_gnu_e_bytes_rejected(self):
        self.prepare_resume()
        with patch.object(rv32, "preprocessed_sha", return_value="f" * 64):
            with self.assertRaisesRegex(ValueError, "GNU-E differs"):
                rv32.restore(self.a)
        self.assertEqual((self.out / self.owner).read_bytes(), elf32_riscv(b"GOLDEN"))

    def test_drifted_compiler_profile_and_generated_input_epochs_rejected(self):
        receipt = self.prepare_resume()
        for key, bad in (("current_compiler_source_profile_sha256", "f" * 64),
                         ("current_minic_compiler_sha256", "f" * 64),
                         ("generated_headers", {"include/generated/autoconf.h": "f" * 64}),
                         ("gnu_as_binary_sha256", "f" * 64)):
            changed = dict(self.identity)
            changed[key] = bad
            rv32.seven.atomic_json(self.now / "checkpoint/identity.json", changed)
            with patch.object(rv32, "preprocessed_sha", return_value=receipt["input_sha256"]):
                with self.assertRaisesRegex(ValueError, "foreign C source/compiler"):
                    rv32.restore(self.a)

    def test_forged_owner_receipt_or_flags_rejected(self):
        receipt = self.prepare_resume()
        path = self.old / "checkpoint/owners" / (self.owner + ".json")
        forged = dict(receipt, cmd_sha256="0" * 64)
        rv32.seven.atomic_json(path, forged)
        with self.assertRaisesRegex(ValueError, "bad old per-owner"):
            rv32.restore(self.a)
        rv32.seven.atomic_json(path, receipt)
        # Same byte hash as the saved record but wrong ELF flags: caller
        # compares against the exact original GNU ELF ABI.
        (self.out / self.owner).write_bytes(elf32_riscv(b"GOLDEN", flags=1))
        with patch.object(rv32, "preprocessed_sha", return_value=receipt["input_sha256"]):
            with self.assertRaisesRegex(ValueError, "RISC-V ABI flags mismatch"):
                rv32.restore(self.a)

    def test_changed_compiler_binary_rejected_on_capture_and_finish(self):
        self.record_success()
        self.minic.write_text("changed!\n")
        with self.assertRaisesRegex(ValueError, "binary drift"):
            rv32.capture(self.a)
        with self.assertRaisesRegex(ValueError, "binary drift"):
            rv32.verify(self.a)

    def test_real_gnu_cmd_sources_from_source_tree_or_generated_out_only(self):
        self.assertEqual(rv32.source_from_command(self.cmd, self.out, self.original),
                         rv32.sha(self.source))
        produced = self.out / "init/generated.c"
        produced.write_text("int generated;\n")
        self.cmd.write_text("savedcmd_init/main.o := riscv64-linux-gnu-gcc -c "
                            "init/generated.c -o init/main.o\n")
        self.assertEqual(rv32.source_from_command(self.cmd, self.out, self.original),
                         rv32.sha(produced))
        outside = self.root / "outside.c"
        outside.write_text("int forbidden;\n")
        self.cmd.write_text("savedcmd_init/main.o := riscv64-linux-gnu-gcc -c "
                            f"{outside} -o init/main.o\n")
        with self.assertRaisesRegex(ValueError, "outside original source/O="):
            rv32.source_from_command(self.cmd, self.out, self.original)


if __name__ == "__main__":
    unittest.main()
