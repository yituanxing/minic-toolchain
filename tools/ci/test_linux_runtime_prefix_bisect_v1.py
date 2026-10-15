#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('linux-runtime-prefix-bisect-v1.py')
spec = importlib.util.spec_from_file_location('prefix_bisect', SCRIPT)
mod = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(mod)


class PrefixBisectTests(unittest.TestCase):
    def setUp(self):
        self.objects = [f'kernel/o{i}.o' for i in range(1, 101)]

    def plan(self, obs):
        return mod.plan(self.objects, obs, 'abc')

    def test_starts_by_proving_baseline(self):
        result = self.plan({})
        self.assertEqual(result['status'], 'NEXT')
        self.assertEqual(result['next_prefix'], 0)

    def test_after_baseline_proves_all_endpoint(self):
        result = self.plan({0: 'PASS'})
        self.assertEqual(result['next_prefix'], 100)

    def test_midpoint(self):
        result = self.plan({0: 'PASS', 100: 'FAIL'})
        self.assertEqual(result['next_prefix'], 50)

    def test_historical_91_92_pattern_isolates_92nd_object(self):
        result = self.plan({91: 'PASS', 92: 'FAIL'})
        self.assertEqual(result['status'], 'ISOLATED')
        self.assertEqual(result['first_bad_index'], 92)
        self.assertEqual(result['first_bad_object'], 'kernel/o92.o')

    def test_inconclusive_midpoint_is_retried_not_used_as_evidence(self):
        result = self.plan({0: 'PASS', 100: 'FAIL', 50: 'INCONCLUSIVE'})
        self.assertEqual(result['status'], 'RETRY')
        self.assertEqual(result['next_prefix'], 50)

    def test_non_monotonic_evidence_stops_search(self):
        result = self.plan({40: 'FAIL', 60: 'PASS'})
        self.assertEqual(result['status'], 'NON_MONOTONIC')

    def test_conflicting_duplicate_observation_rejected(self):
        with self.assertRaises(mod.BisectError):
            mod.parse_observations(['50=PASS', '50=FAIL'], 100)

    def test_object_list_validation(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / 'objects.txt'
            path.write_text('kernel/a.o\n../bad.o\n')
            with self.assertRaises(mod.BisectError):
                mod.load_objects(path)


if __name__ == '__main__':
    unittest.main()
