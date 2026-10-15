#!/usr/bin/env python3
"""Replay frozen GNU-preprocessed failed Linux TUs using only MiniC -S.

This is frontend failure triage: even if MiniC now emits .s, the GNU assembler,
linker and QEMU contracts have NOT yet been checked.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import time

def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for buf in iter(lambda: stream.read(1048576), b''):
            h.update(buf)
    return h.hexdigest()

def validate_obj(raw: str) -> str:
    if raw != raw.strip() or '\\' in raw or '\x00' in raw:
        raise ValueError('invalid object: ' + repr(raw))
    p = PurePosixPath(raw)
    if (p.is_absolute() or len(p.parts)<2 or '..' in p.parts or
            p.as_posix()!=raw or not raw.endswith('.o')):
        raise ValueError('unsafe object: ' + repr(raw))
    return raw

def diagnostic(text: str) -> str:
    lines=[v.strip() for v in text.splitlines() if v.strip()]
    d=next((v for v in lines if 'error:' in v.lower() or
            'fatal:' in v.lower() or 'assert' in v.lower()),
            lines[0] if lines else '(empty stderr)')
    d=re.sub(r'(?:\S+/)?[\w+.-]+\.(?:i|c|h):\d+(?::\d+)?:', '<location>:', d)
    d=re.sub(r'0x[0-9a-fA-F]+', '<hex>', d)
    return d[:220]

def run(args):
    root=args.inputs_root.resolve(strict=True)
    if args.output.resolve() == root or args.output.resolve().is_relative_to(root):
        raise ValueError('output must be outside frozen inputs')
    compiler=args.minic.resolve(strict=True)
    if compiler.is_relative_to(root):
        raise ValueError('compiler must be outside frozen inputs')
    raw=[s.strip() for s in args.blockers.read_text().splitlines() if s.strip()]
    objects=[validate_obj(s) for s in raw]
    if not objects or len(objects)!=len(set(objects)):
        raise ValueError('empty or duplicate blocker list')
    if args.limit<1 or args.timeout<=0:
        raise ValueError('limit and timeout must be positive')
    total=len(objects)
    objects=objects[:args.limit]
    work=args.output.resolve()
    work.mkdir(parents=True,exist_ok=True)
    results=[]
    for i,obj in enumerate(objects):
        relative=obj[:-2]+'.minic-stage2.failed.i'
        source=root/relative
        original_stderr=root/(obj[:-2]+'.minic-stage2.failed.stderr')
        if source.is_symlink() or (source.exists() and not source.resolve().is_relative_to(root)):
            raise ValueError('unsafe frozen input: '+obj)
        row={'object':obj,'input':relative}
        if not source.is_file():
            row.update(status='MISSING_I',input_sha256=None,diagnostic='no frozen .i')
            results.append(row)
            continue
        row['input_sha256']=digest(source)
        assembly=work/('obj-%04d.s' % i)
        assembly.unlink(missing_ok=True)
        start=time.perf_counter_ns()
        try:
            proc=subprocess.run([str(compiler),'-S',str(source),'-o',str(assembly)],
                                stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                timeout=args.timeout,check=False)
            row['exit_code']=proc.returncode
            row['diagnostic']=diagnostic(proc.stderr.decode('utf-8','replace'))
            if proc.returncode==0 and assembly.is_file() and assembly.stat().st_size>0:
                row['status']='FRONTEND_PASS_NEEDS_GNU_AS'
                row['assembly_sha256']=digest(assembly)
            else:
                row['status']='MINIC_FAIL'
                assembly.unlink(missing_ok=True)
        except subprocess.TimeoutExpired:
            row.update(status='MINIC_TIMEOUT',diagnostic='MiniC timed out')
            assembly.unlink(missing_ok=True)
        row['elapsed_ms']=(time.perf_counter_ns()-start)//1000000
        if original_stderr.is_file() and not original_stderr.is_symlink():
            row['original_diagnostic']=diagnostic(original_stderr.read_text(errors='replace')[:16384])
        results.append(row)
    counts={};groups={}
    for row in results:
        counts[row['status']]=counts.get(row['status'],0)+1
        key=row['status']+' | '+row['diagnostic']
        groups.setdefault(key,[]).append(row['object'])
    report={'schema':'minic-linux-failed-tu-replay-v1',
            'verdict':'FRONTEND_ONLY_NOT_RUNTIME_CERTIFIED',
            'compiler_sha256':digest(compiler),
            'total_blockers':total,'replayed_count':len(results),
            'status_counts':counts,'groups':groups,'results':results}
    (work/'replay.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    with (work/'replay.tsv').open('w') as stream:
        stream.write('object\tstatus\tinput_sha256\telapsed_ms\tdiagnostic\n')
        for row in results:
            msg=row['diagnostic'].replace('\t',' ').replace('\n',' ')
            stream.write(f"{row['object']}\t{row['status']}\t{row.get('input_sha256') or '-'}\t{row.get('elapsed_ms',0)}\t{msg}\n")
    print('FAILED_TU_REPLAY=PASS stage=frontend-only '+
          f'tested={len(results)} still_failed={counts.get("MINIC_FAIL",0)} '+
          f'frontend_pass={counts.get("FRONTEND_PASS_NEEDS_GNU_AS",0)} '+
          f'missing_i={counts.get("MISSING_I",0)} groups={len(groups)}')
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--blockers',type=Path,required=True)
    p.add_argument('--inputs-root',type=Path,required=True)
    p.add_argument('--minic',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--limit',type=int,default=32)
    p.add_argument('--timeout',type=float,default=30)
    args=p.parse_args()
    try:
        run(args)
    except (OSError,ValueError,json.JSONDecodeError) as exc:
        print('FAILED_TU_REPLAY=ERROR '+str(exc),file=sys.stderr)
        return 2
    return 0

if __name__=='__main__':
    sys.exit(main())
