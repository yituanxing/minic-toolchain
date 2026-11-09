#!/usr/bin/env python3
"""Fast, no-seed preflight for the current nine-tool bootstrap source owners."""
import importlib.util
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


def script(name):
    path = ROOT / "tools/ci" / name
    spec = importlib.util.spec_from_file_location(name[:-3].replace("-", "_"), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BootstrapSourceInventory(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stage2 = script("minic-bootstrap-stage2-toolchain-v1.py")
        cls.full = script("minic-bootstrap-full-native-v1.py")
        cls.b1 = script("minic-bootstrap-current-b1-seed-v1.py")

    def test_current_manifest_is_actual_expanded_make_order(self):
        make = subprocess.run(
            ["make", "-s", "--eval",
             '__bootstrap_sources: ; @printf "%s\\n" $(MINIC_SOURCES)',
             "__bootstrap_sources"], cwd=ROOT, capture_output=True,
            text=True, check=True)
        actual = (ROOT / "tests/compiler/bootstrap/b0-sources.txt").read_text().splitlines()
        self.assertEqual(actual, make.stdout.splitlines())
        self.assertEqual(actual, self.stage2.source_group(
            (ROOT / "Makefile").read_text(), "MINIC_SOURCES"))
        self.assertTrue(all((ROOT / item).is_file() for item in actual))
        self.assertEqual(len(actual), len(set(actual)))

    def test_nine_tool_inventory_and_b1_use_same_sources(self):
        sources, unique = self.full.inventory()
        self.assertEqual(len(sources), 9)
        self.assertEqual(sources["minic-cc"], self.b1.sources())
        self.assertEqual(set(unique), {src for units in sources.values() for src in units})
        self.assertEqual(len(unique), len(set(unique)))

    def test_nested_literal_source_group_expansion(self):
        data = "A_SOURCES := \\\n a.c \\\n $(B_SOURCES)\nB_SOURCES := \\\n b.c \\\n c.c\n"
        self.assertEqual(self.stage2.source_group(data, "A_SOURCES"),
                         ["a.c", "b.c", "c.c"])

    def test_rejects_bad_source_expansions(self):
        cases = (
            "A_SOURCES := \\\n a.c \\\n a.c\n",  # duplicate
            "A_SOURCES := \\\n $(B_SOURCES)\nB_SOURCES := \\\n $(A_SOURCES)\n",  # cyclic
            "A_SOURCES := \\\n $(shell echo a.c)\n",  # unsupported make function
            "A_SOURCES := \\\n $(MISSING_SOURCES)\n",  # unresolved reference
            "A_SOURCES := \\\n a.c \\\n",  # unterminated group
            "A_SOURCES := \\\n ../outside.c\n",  # path escape
        )
        for data in cases:
            with self.subTest(data=data), self.assertRaises(RuntimeError):
                self.stage2.source_group(data, "A_SOURCES")


if __name__ == "__main__":
    unittest.main()
