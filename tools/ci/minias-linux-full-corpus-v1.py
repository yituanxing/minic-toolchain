#!/usr/bin/env python3
"""Complete 2064 Linux C-object MiniAS assembly sweep, without recompiling .o.

Uses exactly the already certified GNU Kbuild .cmd commands, cached MiniC profile,
and GNU-AS output SHA manifest. Reuses the 64-owner .s corpus (or an earlier
partial/full corpus) and generates only missing .i -> .s; work is parallel on a
single runner, with a SHA-bound checkpoint independent of MiniAS revisions.

This is an OBJECT coverage test only; QEMU and handwritten .S are separate gates.
"""
from __future__ import annotations
import argparse
import concurrent.futures as futures
import importlib.util
import json
import pathlib
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
def load(filename, name):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module

def main():
    c = load('minias-linux-corpus-probe-v1.py', 'minias_sample_probe')
    guard = load('linux-runtime-kbuild-regeneration-guard-v1.py', 'minias_guard')
    p = argparse.ArgumentParser()
    for opt in ('golden-out','candidate-pool','manifest','minic','minias','work','cache-identity'):
        p.add_argument('--'+opt, required=True)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--shard', type=int, default=None)
    p.add_argument('--shards', type=int, default=7)
    args = p.parse_args()
    if args.jobs < 1 or args.jobs > 6 or args.shards!=7 or (args.shard is not None and not 0<=args.shard<7) or not args.cache_identity.startswith('gnu-minic-objects-v1-'):
        raise ValueError('unsafe runner parallelism or invalid corpus cache identity')
    gold = pathlib.Path(args.golden_out).resolve()
    pool = pathlib.Path(args.candidate_pool).resolve()
    manifest = pathlib.Path(args.manifest).resolve()
    minic = pathlib.Path(args.minic).resolve()
    minias = pathlib.Path(args.minias).resolve()
    work = pathlib.Path(args.work).resolve()
    work.mkdir(parents=True,exist_ok=True)
    ref = [x.split(maxsplit=1) for x in manifest.read_text().splitlines() if x.strip()]
    names = [n.removeprefix('minic/') for _,n in ref]
    if len(ref)!=2064 or len(set(names))!=2064 or any(not n.startswith('minic/') for _,n in ref):
        raise ValueError('not exact 2064-object MiniC manifest')
    refs = dict(zip(names, (h for h,_ in ref)))
    for owner in names:
        if c.digest(pool/owner)!=refs[owner]:
            raise ValueError('frozen original changed: '+owner)
    # Keep SAME 64-owner selection prefix as the already accepted probe.
    prefixes=['drivers/tty/serial/earlycon.o','init/','kernel/','mm/','fs/','net/',
              'lib/','arch/','block/','security/','drivers/','crypto/','ipc/','sound/']
    selected=[]
    for prefix in prefixes:
        poss=sorted(n for n in names if n not in selected and
                    (n==prefix if prefix.endswith('.o') else n.startswith(prefix)))
        if poss:selected.append(poss[0])
    selected+=sorted(n for n in names if n not in selected)
    if len(selected)!=2064 or len(set(selected))!=2064:
        raise ValueError('full-corpus target selection incomplete')
    whole_selected=selected
    if args.shard is not None:
        selected=[owner for i,owner in enumerate(whole_selected) if i%7==args.shard]
    total=len(selected)
    sdir=work/'assembly'
    index=sdir/'index.json'
    schema={'schema':'minias-linux-s-corpus-v1','identity':args.cache_identity,
            'minic_sha256':c.digest(minic),'manifest_sha256':c.digest(manifest),
            'config_sha256':c.digest(gold/'.config'),'selected':selected}
    old=json.loads(index.read_text()) if index.is_file() else None
    if old:
        for key,value in schema.items():
            if key!='selected' and old.get(key)!=value:
                raise ValueError('frozen S corpus identity changed: '+key)
        prev=old.get('selected',[])
        if args.shard is None and selected[:len(prev)]!=prev:
            raise ValueError('non-prefix old S corpus, cannot reuse safely')
        if args.shard is not None and not set(prev).issubset(set(whole_selected)):
            raise ValueError('existing S cache includes foreign owners')
    known={} if not old else old.get('files',{})
    if args.shard is not None:
        known={name:info for name,info in known.items() if name in set(selected)}
    if not set(known).issubset(set(selected)):
        raise ValueError('extra frozen S files')
    initial_known=set(known)
    print(f'MINIAS_FULL_SWEEP=START owners={total} of=2064 shard={args.shard} cached_s={len(known)} workers={args.jobs} recompile_C=NO', flush=True)
    def run(owner):
        stem=owner[:-2]
        s=sdir/(stem+'.s')
        log=work/'evidence'/stem
        s.parent.mkdir(parents=True,exist_ok=True)
        original=pool/owner
        cmd=gold/pathlib.Path(owner).parent/('.'+pathlib.Path(owner).name+'.cmd')
        gcc_args=guard.pinned_kbuild_cc_args(gold, owner)
        cmd_hash=c.digest(cmd)
        obj_hash=c.digest(original)
        previous=known.get(owner)
        generated=False
        if previous:
            if not s.is_file() or tuple(previous[k] for k in ('cmd_sha256','original_sha256','s_sha256')) != (cmd_hash,obj_hash,c.digest(s)):
                raise ValueError('S cache SHA mismatch: '+owner)
        else:
            tmp=work/'scratch'/(stem+'.i')
            tmp.parent.mkdir(parents=True,exist_ok=True)
            try:
                rc,_=c.execute(['/usr/bin/riscv64-linux-gnu-gcc','-E','-P',*c.clean_gcc(gcc_args),'-o',str(tmp)],gold,log.with_suffix('.gcc-E.log'),120)
                if rc or not tmp.is_file():return owner,'GCC_E_FAIL',rc,0,None
                rc,_=c.execute([str(minic),'-S',str(tmp),'-o',str(s)],gold,log.with_suffix('.minic-S.log'),210)
                if rc or not s.is_file():return owner,'MINIC_S_FAIL',rc,0,None
            finally:
                tmp.unlink(missing_ok=True)
            generated=True
        if owner=='drivers/tty/serial/earlycon.o' and c.digest(s)!='5a2d6035541018dbacbcc8b7a838f24239d934fd3c3b7fdb3e414e23ddacc02b':
            raise ValueError('frozen earlycon S input diverged')
        item={'cmd_sha256':cmd_hash,'original_sha256':obj_hash,'s_sha256':c.digest(s)}
        flags=c.flags(original)
        arch='rv64gc' if flags&1 else 'rv64g'
        abi={0:'lp64',2:'lp64f',4:'lp64d',6:'lp64q'}[flags&6]
        out=work/'objects'/owner
        out.parent.mkdir(parents=True,exist_ok=True)
        out.unlink(missing_ok=True)
        rc,ms=c.execute([str(minias),'-march='+arch,'-mabi='+abi,'-o',str(out),str(s)],
                        gold,log.with_suffix('.minias.log'),90)
        status='ASSEMBLE_FAIL'
        if rc==0:
            try:
                if c.flags(out)!=flags:status='ELF_FLAGS_MISMATCH'
                elif c.globals_in(out)!=c.globals_in(original):status='GLOBAL_EXPORT_MISMATCH'
                else:status='OBJECT_BASIC_PASS'
            except Exception as e:
                log.with_suffix('.validation.log').write_text(str(e))
                status='ELF_VALIDATE_FAIL'
        return owner,status,rc,ms,item,generated
    rows={}
    changes=0
    with futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
        tasks={executor.submit(run,owner):owner for owner in selected}
        for future in futures.as_completed(tasks):
            owner=tasks[future]
            try:
                res=future.result()
                if len(res)==5: owner,status,rc,ms,item=res;generated=False
                else: owner,status,rc,ms,item,generated=res
            except Exception as exc:
                (work/'evidence'/'python-failures').mkdir(parents=True,exist_ok=True)
                f=work/'evidence'/'python-failures'/(owner.replace('/','__')+'.txt')
                f.write_text(str(exc))
                status='HARNESS_FAIL'
                rc=2
                ms=0
                item=None
                generated=False
            if item is not None:
                known[owner]=item
                changes+=int(generated)
            rows[owner]=(status,rc,ms)
            if len(rows)%24==0:
                sdir.mkdir(parents=True,exist_ok=True)
                index.write_text(json.dumps({**schema,'files':known},sort_keys=True)+'\n')
                print(f'MINIAS_FULL_SWEEP=PROGRESS scanned={len(rows)} of={total} shard={args.shard} s_generated={changes} gaps={sum(v[0]!="OBJECT_BASIC_PASS" for v in rows.values())}',flush=True)
    index.write_text(json.dumps({**schema,'files':known},sort_keys=True)+'\n')
    with (work/'full-scan.tsv').open('w') as f:
        f.write('owner\tstatus\trc\tassemble_ms\n')
        for owner in selected:
            state,rc,ms=rows[owner]
            f.write(f'{owner}\t{state}\t{rc}\t{ms}\n')
    from collections import Counter
    stats=Counter(v[0] for v in rows.values())
    gaps=total-stats['OBJECT_BASIC_PASS']
    summary={'schema':'minias-full-scan-v1','owner_count':total,'full_owner_count':2064,'shard':args.shard,'shards':args.shards,
             'basic_pass':stats['OBJECT_BASIC_PASS'],'gaps':gaps,
             'reused_s':len(initial_known),
             'new_s_generated':changes,'status_counts':dict(stats),
             'gcc_golden_identity':args.cache_identity,'minic_sha256':c.digest(minic),
             'minias_sha256':c.digest(minias),
             'runtime_accepted':False,'linux_handwritten_assembly_covered':False}
    (work/'full-summary.json').write_text(json.dumps(summary,sort_keys=True,indent=2)+'\n')
    print('MINIAS_FULL_SWEEP=COMPLETE '+json.dumps(summary,sort_keys=True),flush=True)
    # Coverage scan is deliberately green when blockers are documented:
    # full runtime gate must NOT assume "complete" means 2064 passing.
    # Other CI steps inspect full-summary.json before attempting native overlay.

if __name__=='__main__':
    try:main()
    except Exception as exc:
        print('MINIAS_FULL_SWEEP=FATAL '+str(exc),file=sys.stderr,flush=True)
        sys.exit(2)
