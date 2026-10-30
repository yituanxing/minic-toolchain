#!/usr/bin/env python3
"""Fast, dependency-free safety tests for the AArch64 GNU object oracle.

Run from any working directory:
    python3 -m unittest discover -s tests/external/linux \
        -p test_minias_aarch64_object_oracle.py -v

The production oracle uses pyelftools for real ELF objects; these tests
exercise its policy boundaries with deliberately controlled semantic
snapshots and fake object paths. Real ELF parity remains a separate CI gate.
"""

from contextlib import redirect_stdout
import copy
import importlib.machinery
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


SOURCE = Path(__file__).with_name("minias_aarch64_object_oracle.py")
SPEC = importlib.util.spec_from_file_location("minias_aarch64_object_oracle", SOURCE)
oracle = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(oracle)


def base_snapshot():
    return {
        "header": {"machine": "EM_AARCH64", "class": 64},
        "section_core": {
            ".text": {"flags": 0x6, "size": 4},
            ".rodata": {"flags": 0x2, "size": 4},
            ".debug_line": {"flags": 0, "size": 3},
        },
        "section_alignment": {".text": 4, ".rodata": 4, ".debug_line": 1},
        "contents_masked": {
            ".text": {"size": 4, "sha256": "code"},
            ".rodata": {"size": 4, "sha256": "data"},
            ".debug_line": {"size": 3, "sha256": "dwarf"},
        },
        "relocations": [
            {"key": [".text", 0, 283,
                     ["SECTION", "STB_LOCAL", "STV_DEFAULT", 0, ".text", 0, 0],
                     0], "count": 1},
            {"key": [".debug_line", 0, 257,
                     ["SECTION", "STB_LOCAL", "STV_DEFAULT", 0, ".text", 0, 0],
                     0], "count": 1},
        ],
        "symbols": [
            {"key": ["NAMED", "extern", "STB_GLOBAL", "STT_NOTYPE",
                     "STV_DEFAULT", 0, "SHN_UNDEF", 0, 0], "count": 1},
        ],
        "groups": [],
        "mapping_symbols": [
            {"key": ["$x", ".text", 0], "count": 1},
            {"key": ["$d", ".rodata", 0], "count": 1},
        ],
        "relocation_order": {},
        "content_skipped_unknown_reloc_width": {},
    }


class FakeSemanticOracle:
    def __init__(self, reference, candidate):
        self.snapshots = [reference, candidate]

    def load_semantics(self, _path):
        return self.snapshots.pop(0)

    @staticmethod
    def alignment_compatible(reference, candidate):
        return (reference.keys() == candidate.keys() and all(
            candidate[name] >= value and candidate[name] % value == 0
            for name, value in reference.items()
        ))


class TestAllocatedSemantics(unittest.TestCase):
    def verdict(self, candidate):
        return oracle.compare_allocated(
            FakeSemanticOracle(base_snapshot(), candidate),
            Path("reference.o"), Path("candidate.o"))

    def test_debug_only_content_and_section_metadata_do_not_change_alloc(self):
        candidate = base_snapshot()
        candidate["section_core"][".debug_line"]["size"] = 400
        candidate["contents_masked"][".debug_line"]["sha256"] = "different"
        candidate["relocations"][1]["key"][2] = 258
        self.assertEqual(self.verdict(candidate), ("ALLOC_EQUAL", "-"))

    def test_debug_only_comdat_group_does_not_change_alloc(self):
        candidate = base_snapshot()
        candidate["groups"] = [
            {"signature": "debug-only", "occurrence": 0,
             "flags": 1, "members": [".debug_line"]}
        ]
        self.assertEqual(self.verdict(candidate), ("ALLOC_EQUAL", "-"))

    def test_loaded_text_byte_change_fails(self):
        candidate = base_snapshot()
        candidate["contents_masked"][".text"]["sha256"] = "corrupted"
        self.assertEqual(self.verdict(candidate),
                         ("ALLOC_DIFF", "contents_masked"))

    def test_loaded_rodata_byte_change_fails(self):
        candidate = base_snapshot()
        candidate["contents_masked"][".rodata"]["sha256"] = "corrupted"
        self.assertEqual(self.verdict(candidate),
                         ("ALLOC_DIFF", "contents_masked"))

    def test_runtime_relocation_type_change_fails(self):
        candidate = base_snapshot()
        candidate["relocations"][0]["key"][2] = 282
        self.assertEqual(self.verdict(candidate),
                         ("ALLOC_DIFF", "relocations_link_effective"))

    def test_runtime_relocation_addend_change_fails(self):
        candidate = base_snapshot()
        candidate["relocations"][0]["key"][-1] = 4
        self.assertEqual(self.verdict(candidate),
                         ("ALLOC_DIFF", "relocations_link_effective"))

    def test_local_section_and_function_same_address_are_link_equivalent(self):
        candidate = base_snapshot()
        candidate["relocations"][0]["key"][3] = [
            "LOCAL", "STT_FUNC", "STV_DEFAULT", 0, ".text", 0, 16]
        self.assertEqual(self.verdict(candidate), ("ALLOC_EQUAL", "-"))

    def test_nonzero_symbol_other_bits_cannot_be_normalized_away(self):
        candidate = base_snapshot()
        candidate["relocations"][0]["key"][3] = [
            "LOCAL", "STT_FUNC", "STV_DEFAULT", 1, ".text", 0, 16]
        self.assertEqual(self.verdict(candidate),
                         ("ALLOC_DIFF", "relocations_link_effective"))

    def test_executable_mapping_symbol_boundary_change_fails(self):
        candidate = base_snapshot()
        candidate["mapping_symbols"][0]["key"][2] = 2
        self.assertEqual(self.verdict(candidate),
                         ("ALLOC_DIFF", "executable_mapping"))

    def test_nonexecutable_mapping_symbol_difference_is_diagnostic_only(self):
        candidate = base_snapshot()
        candidate["mapping_symbols"][1]["key"][2] = 2
        self.assertEqual(self.verdict(candidate), ("ALLOC_EQUAL", "-"))

    def test_unknown_loaded_relocation_fails_closed_as_inconclusive(self):
        candidate = base_snapshot()
        candidate["content_skipped_unknown_reloc_width"][".text"] = [
            [0, 999, "unknown-target-relocation-fields"]]
        self.assertEqual(self.verdict(candidate),
                         ("ALLOC_INCONCLUSIVE", "unknown_relocation_fields"))

    def test_unknown_dwarf_relocation_does_not_taint_alloc_verdict(self):
        candidate = base_snapshot()
        candidate["content_skipped_unknown_reloc_width"][".debug_line"] = [
            [0, 999, "unknown-target-relocation-fields"]]
        self.assertEqual(self.verdict(candidate), ("ALLOC_EQUAL", "-"))

    def test_external_symbol_identity_change_fails(self):
        candidate = base_snapshot()
        candidate["symbols"][0]["key"][1] = "different_extern"
        self.assertEqual(self.verdict(candidate),
                         ("ALLOC_DIFF", "global_symbols"))

    def test_stricter_alloc_alignment_is_compatible(self):
        candidate = base_snapshot()
        candidate["section_alignment"][".text"] = 16
        self.assertEqual(self.verdict(candidate), ("ALLOC_EQUAL", "-"))

    def test_weaker_alloc_alignment_fails(self):
        candidate = base_snapshot()
        candidate["section_alignment"][".text"] = 2
        self.assertEqual(self.verdict(candidate),
                         ("ALLOC_DIFF", "alignment"))

    def test_transient_probe_filter_is_narrow(self):
        self.assertTrue(oracle.is_transient_probe(Path(".tmp_5103/tmp.o")))
        self.assertFalse(oracle.is_transient_probe(Path(".tmp_X/tmp.o")))
        self.assertFalse(oracle.is_transient_probe(Path(".tmp_5103/kernel.o")))
        self.assertFalse(oracle.is_transient_probe(Path("a/.tmp_5103/tmp.o")))
        self.assertFalse(oracle.is_transient_probe(Path("arch/arm64/mm/proc.o")))


class NoopOracleLoader:
    """main() must classify missing objects without needing pyelftools."""

    @staticmethod
    def create_module(_spec):
        return None

    @staticmethod
    def exec_module(module):
        module.compare_paths = lambda *_args: {"strict_equal": True,
                                                "inconclusive": False,
                                                "dimensions": {},
                                                "strict_mapping_symbols": True}


class TestProductionMissingPolicy(unittest.TestCase):
    def execute(self, reference_names):
        with tempfile.TemporaryDirectory(prefix="minias-a64-oracle-") as scratch:
            root = Path(scratch)
            gnu = root / "gnu"
            mini = root / "mini"
            gnu.mkdir()
            mini.mkdir()
            for relative in reference_names:
                destination = gnu / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(b"fake-reference-object")
            args = ["minias_aarch64_object_oracle.py",
                    "--gnu-dir", str(gnu),
                    "--mini-dir", str(mini),
                    "--out", str(root / "result.tsv")]
            spec = importlib.machinery.ModuleSpec(
                "dummy_elf_semantic_compare", NoopOracleLoader())
            printed = io.StringIO()
            with (patch.object(sys, "argv", args),
                  patch.object(oracle.importlib.util, "spec_from_file_location",
                               return_value=spec),
                  redirect_stdout(printed)):
                result = oracle.main()
            return result, printed.getvalue(), (root / "result.tsv").read_text()

    def test_disposable_kbuild_probe_is_excluded(self):
        result, summary, rows = self.execute([".tmp_5103/tmp.o"])
        self.assertEqual(result, 0)
        self.assertIn("missing=0", summary)
        self.assertIn("probe_ignored=1", summary)
        self.assertIn("PROBE_IGNORED\t.tmp_5103/tmp.o", rows)

    def test_real_production_missing_object_fails_closed(self):
        result, summary, rows = self.execute([
            ".tmp_5103/tmp.o", "arch/arm64/mm/proc.o"])
        self.assertEqual(result, 1)
        self.assertIn("missing=1", summary)
        self.assertIn("probe_ignored=1", summary)
        self.assertIn("MISSING\tarch/arm64/mm/proc.o", rows)
        self.assertIn("status=FAIL", summary)


if __name__ == "__main__":
    unittest.main()
