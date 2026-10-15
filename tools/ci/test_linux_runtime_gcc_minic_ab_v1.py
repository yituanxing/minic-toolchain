#!/usr/bin/env python3
"""Offline tests: immutable GNU input/outputs, iterate-only MiniC, and failures."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).with_name('linux-runtime-gcc-minic-ab-v1.py')
spec = importlib.util.spec_from_file_location('gnu_locked_tu', SCRIPT)
mod = importlib.util.module_from_spec(spec)
assert spec and spec.loader
spec.loader.exec_module(mod)

MOCK_TOOL = r'''#!/usr/bin/env python3
import hashlib
import os
from pathlib import Path
import struct
import sys
args = sys.argv[1:]
if '-S' in args:
    source = Path(args[args.index('-S')+1])
    # GNU invocation is '-x cpp-output -S <input> -o output'.
    if not source.is_file():
        source = Path(args[args.index('-o')-1])
    output = Path(args[args.index('-o')+1])
    version = Path(__file__).stem
    output.write_text(version + ':' + hashlib.sha256(source.read_bytes()).hexdigest() + '\n')
elif '-c' in args:
    source = Path(args[args.index('-c')+1])
    output = Path(args[args.index('-o')+1])
    head = bytearray(64)
    head[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<H', head, 16, 1)
    struct.pack_into('<H', head, 18, 243)
    struct.pack_into('<I', head, 48, 0)
    output.write_bytes(head + source.read_bytes())
else:
    raise SystemExit('unsupported')
with open(os.environ['GNU_LOCKED_MOCK_TRACE'], 'a') as f:
    f.write(Path(__file__).stem + ' ' + ' '.join(args) + '\n')
'''


class GNUABTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.input = self.root / 'init.i'
        self.config = self.root / '.config'
        self.gcc_flags = self.root / 'gcc-flags.txt'
        self.as_flags = self.root / 'as-flags.txt'
        self.gnu = self.root / 'gnu'
        self.minic = self.root / 'minic'
        self.work = self.root / 'work'
        self.trace = self.root / 'calls.txt'
        self.input.write_text('int init(void) { return 17; }\n')
        self.config.write_text('CONFIG_RISCV=y\n')
        self.gcc_flags.write_text('-O2\n-mabi=lp64\n')
        self.as_flags.write_text('-mabi=lp64\n')
        for exe in (self.gnu, self.minic):
            exe.write_text(MOCK_TOOL)
            exe.chmod(0o755)
        self.old_env = __import__('os').environ.get('GNU_LOCKED_MOCK_TRACE')
        __import__('os').environ['GNU_LOCKED_MOCK_TRACE'] = str(self.trace)

    def tearDown(self):
        if self.old_env is None:
            __import__('os').environ.pop('GNU_LOCKED_MOCK_TRACE', None)
        else:
            __import__('os').environ['GNU_LOCKED_MOCK_TRACE'] = self.old_env
        self.tmp.cleanup()

    def execute(self, mode):
        return mod.main(['--mode', mode, '--object', 'kernel/init.o',
                         '--input', str(self.input), '--config', str(self.config),
                         '--gcc-flags', str(self.gcc_flags), '--as-flags', str(self.as_flags),
                         '--gnu-cc', str(self.gnu), '--minic', str(self.minic),
                         '--work', str(self.work)])

    def test_establish_then_iterate_no_gcc_compilation_or_baseline_mutation(self):
        self.assertEqual(self.execute('establish'), 0)
        original_gcc = (self.work / 'gnu/reference.o').read_bytes()
        original_calls = self.trace.read_text().splitlines()
        self.assertEqual(len(original_calls), 4)
        self.assertEqual(self.execute('iterate'), 0)
        self.assertEqual((self.work / 'gnu/reference.o').read_bytes(), original_gcc)
        after = self.trace.read_text().splitlines()
        self.assertEqual(len(after), 6)
        self.assertTrue(after[4].startswith('minic '))
        self.assertTrue(after[5].startswith('gnu '))
        report = json.loads((self.work / 'last-experiment.json').read_text())
        self.assertEqual(report['verdict'], 'OBJECTS_BUILT_NOT_RUNTIME_CERTIFIED')

    def test_changed_i_refused_without_compilation(self):
        self.assertEqual(self.execute('establish'), 0)
        before = self.trace.read_text()
        self.input.write_text('int init(void) { return 18; }\n')
        self.assertEqual(self.execute('iterate'), 2)
        self.assertEqual(self.trace.read_text(), before)

    def test_changed_flags_or_config_refused(self):
        self.assertEqual(self.execute('establish'), 0)
        self.as_flags.write_text('-mabi=lp64d\n')
        self.assertEqual(self.execute('iterate'), 2)
        self.as_flags.write_text('-mabi=lp64\n')
        self.config.write_text('CONFIG_RISCV=n\n')
        self.assertEqual(self.execute('iterate'), 2)

    def test_tampered_gcc_reference_refused(self):
        self.assertEqual(self.execute('establish'), 0)
        p = self.work / 'gnu/reference.o'
        p.write_bytes(p.read_bytes() + b'other')
        self.assertEqual(self.execute('iterate'), 2)

    def test_missing_baseline_refused(self):
        self.assertEqual(self.execute('iterate'), 2)

    def test_cannot_overwrite_baseline(self):
        self.assertEqual(self.execute('establish'), 0)
        self.assertEqual(self.execute('establish'), 2)

    def test_bad_flags_refused(self):
        self.gcc_flags.write_text('-o\nfoo\n')
        self.assertEqual(self.execute('establish'), 2)

    def test_candidate_failure_does_not_reuse_old_object(self):
        self.assertEqual(self.execute('establish'), 0)
        self.minic.write_text('#!/bin/sh\nexit 4\n')
        self.minic.chmod(0o755)
        self.assertEqual(self.execute('iterate'), 2)
        self.assertFalse((self.work / 'minic/candidate.o').exists())


if __name__ == '__main__':
    unittest.main()
