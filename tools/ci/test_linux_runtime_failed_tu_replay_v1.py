#!/usr/bin/env python3
"""Offline tests for unchanged frozen input and target-only MiniC replay."""
from __future__ import annotations
import importlib.util
from pathlib import Path
import tempfile
import unittest

SOURCE=Path(__file__).with_name('linux-runtime-failed-tu-replay-v1.py')
spec=importlib.util.spec_from_file_location('replay',SOURCE)
mod=importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(mod)

class ReplayTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.root=Path(self.tmp.name)
        self.inputs=self.root/'inputs'; self.inputs.mkdir()
        self.out=self.root/'out'
        self.minic=self.root/'minic'
        self.minic.write_text('#!/usr/bin/env python3\nimport sys\nfrom pathlib import Path\na=sys.argv\ns=Path(a[a.index("-S")+1]).read_text()\nif "FAIL" in s:\n print("foo.i:101: error: invalid operand",file=sys.stderr)\n sys.exit(1)\nPath(a[a.index("-o")+1]).write_text("asm:"+s)\n')
        self.minic.chmod(0o755)
        self.names=['kernel/a.o','lib/b.o','fs/missing.o']
        for name,body in zip(self.names[:2],['FAIL\n','OK\n']):
            path=self.inputs/(name[:-2]+'.minic-stage2.failed.i')
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text(body)
        self.blockers=self.root/'blockers.txt'
        self.blockers.write_text('\n'.join(self.names)+'\n')

    def tearDown(self):
        self.tmp.cleanup()

    def args(self):
        import argparse
        return argparse.Namespace(blockers=self.blockers,inputs_root=self.inputs,
                                  minic=self.minic,output=self.out,limit=32,timeout=5)

    def test_replay_and_immutable_inputs(self):
        source=self.inputs/'kernel/a.minic-stage2.failed.i'
        original=source.read_bytes()
        result=mod.run(self.args())
        self.assertEqual(result['replayed_count'],3)
        self.assertEqual(result['status_counts'],{
            'MINIC_FAIL':1,'FRONTEND_PASS_NEEDS_GNU_AS':1,'MISSING_I':1})
        self.assertTrue(any('invalid operand' in s for s in result['groups']))
        self.assertEqual(source.read_bytes(),original)
        self.assertTrue((self.out/'replay.json').is_file())
        self.assertEqual(result['verdict'],'FRONTEND_ONLY_NOT_RUNTIME_CERTIFIED')

    def test_bad_paths(self):
        for name in ('../bad.o','/abs/x.o','kernel/../../bad.o','kernel\bad.o'):
            with self.assertRaises(ValueError): mod.validate_obj(name)

    def test_explicit_partial_replay(self):
        opts=self.args();opts.limit=1
        result=mod.run(opts)
        self.assertEqual(result['total_blockers'],3)
        self.assertEqual(result['replayed_count'],1)

    def test_symlink_blocked(self):
        src=self.inputs/'kernel/a.minic-stage2.failed.i'
        src.unlink();src.symlink_to(self.minic)
        with self.assertRaises(ValueError):mod.run(self.args())

if __name__=='__main__':
    unittest.main()
