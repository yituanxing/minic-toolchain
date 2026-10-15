#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('linux-runtime-object-set-v1.py')
spec = importlib.util.spec_from_file_location('object_set', SCRIPT)
mod = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(mod)


def elf_rel(payload: bytes) -> bytes:
    h = bytearray(64)
    h[:4] = b'\x7fELF'
    h[4] = 2
    h[5] = 1
    h[6] = 1
    struct.pack_into('<H', h, 16, 1)
    return bytes(h) + payload


class ObjectSetTests(unittest.TestCase):
    def setUp(self):
        self.td = tempfile.TemporaryDirectory()
        self.root = Path(self.td.name)
        self.base = self.root / 'base'
        self.cand = self.root / 'cand'
        self.work = self.root / 'work'
        for root in (self.base, self.cand):
            (root / 'kernel').mkdir(parents=True)
            (root / 'fs').mkdir(parents=True)
        self.objects = ['kernel/a.o', 'kernel/b.o', 'fs/c.o']
        for i, rel in enumerate(self.objects):
            (self.base / rel).write_bytes(elf_rel(f'gcc-{i}'.encode()))
            (self.cand / rel).write_bytes(elf_rel(f'minic-{i}'.encode()))
        self.list = self.root / 'objects.txt'
        self.list.write_text('\n'.join(self.objects) + '\n')

    def tearDown(self):
        self.td.cleanup()

    def args(self, mode, **kw):
        return argparse.Namespace(
            baseline_root=self.base,
            candidate_root=self.cand,
            work_root=self.work,
            objects_file=self.list,
            mode=mode,
            prefix=kw.get('prefix'),
            object=kw.get('objects', []),
            manifest=kw.get('manifest'),
            dry_run=kw.get('dry_run', False),
        )

    def test_prefix_restores_stale_then_overlays(self):
        self.work.mkdir()
        for rel in self.objects:
            p = self.work / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(elf_rel(b'stale'))
        manifest = mod.prepare(self.args('prefix', prefix=2))
        self.assertEqual(manifest['selected_objects'], self.objects[:2])
        self.assertEqual((self.work / 'kernel/a.o').read_bytes(), (self.cand / 'kernel/a.o').read_bytes())
        self.assertEqual((self.work / 'kernel/b.o').read_bytes(), (self.cand / 'kernel/b.o').read_bytes())
        self.assertEqual((self.work / 'fs/c.o').read_bytes(), (self.base / 'fs/c.o').read_bytes())
        self.assertEqual(manifest['objects'][2]['provenance'], 'gcc-baseline')

    def test_single_and_all_except(self):
        one = mod.prepare(self.args('single', objects=['fs/c.o']))
        self.assertEqual(one['selected_objects'], ['fs/c.o'])
        other = mod.prepare(self.args('all-except', objects=['kernel/b.o']))
        self.assertEqual(other['selected_objects'], ['kernel/a.o', 'fs/c.o'])

    def test_duplicate_list_rejected(self):
        self.list.write_text('kernel/a.o\nkernel/a.o\n')
        with self.assertRaises(mod.ObjectSetError):
            mod.prepare(self.args('baseline'))

    def test_unsafe_path_rejected(self):
        self.list.write_text('../evil.o\n')
        with self.assertRaises(mod.ObjectSetError):
            mod.prepare(self.args('baseline'))

    def test_missing_candidate_rejected_before_mutation(self):
        (self.cand / 'kernel/b.o').unlink()
        with self.assertRaises(mod.ObjectSetError):
            mod.prepare(self.args('prefix', prefix=2))
        self.assertFalse(self.work.exists())

    def test_non_rel_elf_rejected(self):
        bad = bytearray(elf_rel(b'x'))
        struct.pack_into('<H', bad, 16, 2)
        (self.cand / 'kernel/a.o').write_bytes(bad)
        with self.assertRaises(mod.ObjectSetError):
            mod.prepare(self.args('single', objects=['kernel/a.o']))

    def test_manifest_has_hash_provenance(self):
        manifest_path = self.root / 'evidence' / 'manifest.json'
        manifest = mod.prepare(self.args('single', objects=['kernel/a.o'], manifest=manifest_path))
        self.assertTrue(manifest_path.is_file())
        entry = manifest['objects'][0]
        self.assertEqual(entry['provenance'], 'minic')
        self.assertEqual(entry['candidate_sha256'], entry['result_sha256'])
        self.assertNotEqual(entry['baseline_sha256'], entry['candidate_sha256'])

    def test_dry_run_has_no_writes(self):
        manifest_path = self.root / 'manifest.json'
        manifest = mod.prepare(self.args('all', dry_run=True, manifest=manifest_path))
        self.assertEqual(manifest['selected_count'], 3)
        self.assertFalse(self.work.exists())
        self.assertFalse(manifest_path.exists())


if __name__ == '__main__':
    unittest.main()
