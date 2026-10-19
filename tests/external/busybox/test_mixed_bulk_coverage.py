#!/usr/bin/env python3
"""Unit-test certified BusyBox MiniC-vs-GCC object ownership boundaries.

Fast synthetic test; no upstream BusyBox downloads or full Kbuild required.
The real Kbuild test separately authenticates original object identity.
"""
import unittest

from mixed_bulk_kbuild import (
    CERTIFIED_GCC_FALLBACKS,
    CERTIFIED_MINIC_OWNERS,
    certified_owner_coverage,
)


class BulkOwnershipCoverageTests(unittest.TestCase):
    def setUp(self):
        self.mini = [f"certified/owner-{i:04d}.o"
                     for i in range(CERTIFIED_MINIC_OWNERS)]
        self.gnu = sorted(CERTIFIED_GCC_FALLBACKS)
        self.assertEqual(len(self.gnu), 12)

    def test_certified_baseline_passes(self):
        self.assertTrue(certified_owner_coverage(self.mini, self.gnu))

    def test_single_minic_owner_reverts_to_gcc_is_rejected(self):
        # The old 580/13 acceptance would have incorrectly passed.
        self.assertFalse(certified_owner_coverage(
            self.mini[:-1], self.gnu + [self.mini[-1]]))

    def test_unchanged_counts_new_gcc_fallback_is_rejected(self):
        self.assertFalse(certified_owner_coverage(
            self.mini[:-1] + [self.gnu[0]],
            self.gnu[1:] + [self.mini[-1]]))

    def test_missing_or_duplicate_owner_is_rejected(self):
        self.assertFalse(certified_owner_coverage(self.mini[:-1], self.gnu))
        self.assertFalse(certified_owner_coverage(
            self.mini[:-1] + [self.mini[0]], self.gnu))

    def test_replacing_a_known_gcc_fallback_with_minic_is_allowed(self):
        self.assertTrue(certified_owner_coverage(
            self.mini + [self.gnu[0]], self.gnu[1:]))


if __name__ == "__main__":
    unittest.main()
