#!/usr/bin/env python3
"""Form an isolated, SHA-attested MiniAS overlay for the existing Linux P0/P1 runner.

Consumes ONLY the frozen 2064 MiniC objects and independently cached MiniC .s
+ current MiniAS outputs from minias-linux-corpus-probe-v1.py. Never recompiles
C or alters golden Kbuild reference. Excludes four vDSO-header-sensitive TUs,
which Kbuild legitimately replays against changed generated headers.
"""
import argparse, concurrent.futures, hashlib, json, pathlib, shutil, struct, subprocess, sys

DYNAMIC_HEADERS = {
    'arch/riscv/kernel/alternative.o',
    'arch/riscv/kernel/signal.o',
    'arch/riscv/kernel/compat_signal.o',
    'arch/riscv/kernel/vdso.o',
}
EARLYCON = 'drivers/tty/serial/earlycon.o'

def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda:f.read(1048576),b''): h.update(block)
    return h.hexdigest()

def elf_flags(p):
    b=p.read_bytes()[:52]
    if len(b)!=52 or b[:6]!=b'\x7fELF\x02\x01':
        raise ValueError('not ELF64LE: '+str(p))
    if struct.unpack_from('<HH',b,16)!=(1,243):
        raise ValueError('not RV64 ET_REL: '+str(p))
    return struct.unpack_from('<I',b,48)[0]

def exports(p):
    r=subprocess.run(['riscv64-linux-gnu-nm','-g','--defined-only','-P',str(p)],
                     check=True,capture_output=True,text=True)
    return set(row.split(' ',1)[0] for row in r.stdout.splitlines() if row)

def seven_shard_overlay(a, frozen, manifest, orig, output, evidence):
    """Upgrade the accepted 64-object overlay to 2060 native C objects.

    Read all seven SHA-indexed exact-MiniC .s artifact shards produced by the
    EXISTING gcc-shards matrix. Reassemble with the CURRENT native MiniAS and
    feed the existing verified full-Linux P1 runner, changing no GNU baseline.
    The four vdso-offsets.h dependents are refreshed natively after first link.
    """
    runner=pathlib.Path(a.seven_shard_root).resolve()
    assembler=pathlib.Path(a.minias).resolve()
    compiler=pathlib.Path(a.minic).resolve()
    golden=pathlib.Path(a.golden_out).resolve()
    if not runner.is_dir() or not assembler.is_file() or not compiler.is_file():
        raise ValueError('missing seven-shard artifacts, MiniAS or MiniC')
    identity_sha=sha(manifest)
    compiler_sha=sha(compiler)
    config_sha=sha(golden/'.config')
    collected={}
    shard_ids=set()
    indexes=list(runner.rglob('assembly/index.json'))
    if len(indexes)!=7:
        raise ValueError('expected 7 independently SHA-indexed MiniC S shards, got '+str(len(indexes)))
    for p in indexes:
        scan=p.parent.parent
        index=json.loads(p.read_text())
        summary=json.loads((scan/'full-summary.json').read_text())
        scope=index.get('selected')
        files=index.get('files')
        sid=summary.get('shard')
        if sid not in range(7) or sid in shard_ids or summary.get('shards')!=7:
            raise ValueError('invalid/duplicate seven-shard identity: '+str(sid))
        shard_ids.add(sid)
        if (index.get('schema')!='minias-linux-s-corpus-v1' or
            index.get('identity')!=a.cache_identity or
            index.get('manifest_sha256')!=identity_sha or
            index.get('minic_sha256')!=compiler_sha or
            index.get('config_sha256')!=config_sha):
            raise ValueError('frozen MiniC / Kbuild / cache profile mismatch, shard='+str(sid))
        if (not isinstance(scope,list) or not isinstance(files,dict) or
            set(scope)!=set(files) or summary.get('owner_count')!=len(scope) or
            summary.get('basic_pass')!=len(scope) or summary.get('gaps')!=0):
            raise ValueError('incomplete native assembly scan, shard='+str(sid))
        lines=(scan/'full-scan.tsv').read_text().splitlines()
        if not lines or lines[0]!='owner\tstatus\trc\tassemble_ms' or len(lines)!=len(scope)+1:
            raise ValueError('malformed full-scan TSV, shard='+str(sid))
        checked={}
        for line in lines[1:]:
            row=line.split('\t')
            if len(row)!=4 or row[1]!='OBJECT_BASIC_PASS' or row[2]!='0' or row[0] in checked:
                raise ValueError('unverified scanned owner, shard='+str(sid))
            checked[row[0]]=True
        if set(checked)!=set(scope):
            raise ValueError('scanned owner mismatches SHA index, shard='+str(sid))
        for owner in scope:
            if owner in collected or owner not in orig:
                raise ValueError('overlap or foreign owner in seven-way MiniC S corpus: '+owner)
            assembly=p.parent/(owner[:-2]+'.s')
            data=files[owner]
            if not assembly.is_file() or sha(assembly)!=data.get('s_sha256'):
                raise ValueError('MiniC S payload SHA mismatch: '+owner)
            cmd=golden/pathlib.Path(owner).parent/('.'+pathlib.Path(owner).name+'.cmd')
            if (data.get('original_sha256')!=orig[owner] or
                not cmd.is_file() or sha(cmd)!=data.get('cmd_sha256')):
                raise ValueError('frozen baseline provenance mismatch: '+owner)
            collected[owner]=assembly
    if shard_ids!=set(range(7)) or set(collected)!=set(orig):
        raise ValueError('seven-way corpus not exactly 2064 C objects; missing='+
                         str(len(set(orig)-set(collected))))
    if output.exists():raise ValueError('native experimental object pool must be new')
    shutil.copytree(frozen,output,copy_function=shutil.copy2)
    scratch=evidence/'reassembly'
    scratch.mkdir(parents=True,exist_ok=True)
    def native(owner):
        baseline=frozen/owner
        raw=elf_flags(baseline)
        abi={0:'lp64',2:'lp64f',4:'lp64d',6:'lp64q'}[raw&6]
        arch='rv64gc' if raw&1 else 'rv64g'
        target=scratch/owner
        target.parent.mkdir(parents=True,exist_ok=True)
        result=subprocess.run([str(assembler),'-march='+arch,'-mabi='+abi,
                               '-o',str(target),str(collected[owner])],
                              stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
        if result.returncode or not target.is_file():
            raise ValueError('MiniAS failed owner='+owner+' rc='+str(result.returncode)+
                             ' log='+result.stdout[-1200:].decode('utf-8','replace'))
        if elf_flags(target)!=raw or exports(target)!=exports(baseline):
            raise ValueError('MiniAS ELF flags/exports mismatch, owner='+owner)
        return owner,sha(target),raw
    promoted=[]
    failures=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        tasks={pool.submit(native,owner):owner for owner in sorted(collected)}
        for future in concurrent.futures.as_completed(tasks):
            owner=tasks[future]
            try:
                name,h,f= future.result()
                if name not in DYNAMIC_HEADERS:
                    shutil.copy2(scratch/name,output/name)
                    if sha(output/name)!=h:raise ValueError('native overlay SHA drift: '+name)
                    promoted.append((name,h,f))
            except Exception as exc:
                failures.append((owner,str(exc)))
    if failures:
        (evidence/'assembly-failures.tsv').write_text(
            'owner\tproblem\n' + ''.join(name+'\t'+msg.replace('\n',' ')+'\n' for name,msg in failures))
        raise ValueError(f'full native reassembly did not pass for {len(failures)} objects; see assembly-failures.tsv')
    if len(promoted)!=2060:
        raise ValueError('expected 2060 native C objects before generated-header refresh, got '+str(len(promoted)))
    chosen={name for name,_,_ in promoted}
    for name,h in orig.items():
        if name not in chosen and sha(output/name)!=h:
            raise ValueError('frozen dynamic-header object unexpectedly changed: '+name)
    promoted.sort()
    with (evidence/'native-object-hashes.sha256').open('w') as out:
        for name,h,f in promoted:out.write(f'{h}  {name}\n')
    (evidence/'overlay-provenance.json').write_text(json.dumps({
        'schema':'minias-runtime-overlay-v1','scope':'ALL_LINUX_C_2064',
        'cache_identity':a.cache_identity,'original_manifest_sha256':identity_sha,
        'source_shards':7,'MiniAS_C_owners_before_generated_header_refresh':len(promoted),
        'deferred_four_generated_header_owners':sorted(DYNAMIC_HEADERS),
        'fresh_original_C_object_compilation':0,'runtime_accepted':False,
    },indent=2,sort_keys=True)+'\n')
    print('MINIAS_RUNTIME_POOL=PASS scanned=2064 native=2060 deferred_generated_headers=4 frozen_original_SHA_attested=2064 C_recompiled=0',flush=True)

def main():
    p=argparse.ArgumentParser()
    for a in ('original-pool','manifest','scan-root','destination','evidence','cache-identity'):
        p.add_argument('--'+a,required=True)
    for extra in ('seven-shard-root','minias','minic','golden-out'):
        p.add_argument('--'+extra)
    a=p.parse_args()
    frozen=pathlib.Path(a.original_pool).resolve()
    manifest=pathlib.Path(a.manifest).resolve()
    work=pathlib.Path(a.scan_root).resolve()
    output=pathlib.Path(a.destination).resolve()
    evidence=pathlib.Path(a.evidence).resolve()
    evidence.mkdir(parents=True,exist_ok=True)
    record=[s.split(maxsplit=1) for s in manifest.read_text().splitlines() if s.strip()]
    if len(record)!=2064 or len({n for _,n in record})!=2064:
        raise ValueError('expected complete immutable 2064-object cache')
    orig={}
    for h,name in record:
        if not name.startswith('minic/'):raise ValueError(name)
        key=name.removeprefix('minic/')
        f=frozen/key
        if not f.is_file() or sha(f)!=h:raise ValueError('certified original differs: '+key)
        orig[key]=h
    if a.seven_shard_root:
        seven_shard_overlay(a,frozen,manifest,orig,output,evidence)
        return
    index=json.loads((work/'assembly/index.json').read_text())
    if index.get('schema')!='minias-linux-s-corpus-v1' or index.get('identity')!=a.cache_identity:
        raise ValueError('S corpus identity mismatch')
    if index.get('manifest_sha256')!=sha(manifest):
        raise ValueError('frozen manifest mismatch')
    selected=index.get('selected')
    if not isinstance(selected,list) or len(selected)!=64 or len(set(selected))!=64:
        raise ValueError('not exactly 64-scanned candidate owners')
    if EARLYCON not in selected or not DYNAMIC_HEADERS <= set(selected):
        raise ValueError('missing canonical owners/header-sensitive exclusions')
    rows=(work/'scan.tsv').read_text().splitlines()
    if not rows or rows[0]!='owner\tstatus\trc\tms\te_flags':
        raise ValueError('missing/invalid MiniAS scan report')
    scanned={}
    for row in rows[1:]:
        parts=row.split('\t')
        if len(parts)!=5:raise ValueError('invalid scan TSV')
        name,status,rc,ms,ef=parts
        if name in scanned or status!='OBJECT_BASIC_PASS' or rc!='0':
            raise ValueError('MiniAS basic validation not complete: '+name)
        scanned[name]=ef
    if set(scanned)!=set(selected):raise ValueError('scan/s selection mismatch')
    if output.exists():raise ValueError('experimental pool must be new (never overwrite)')
    shutil.copytree(frozen,output,copy_function=shutil.copy2)
    promoted=[]
    with (evidence/'promotion.tsv').open('w') as f:
        f.write('owner\tresult\told_sha256\tnew_sha256\telf_flags\n')
        for name in selected:
            s=work/'assembly'/(name[:-2]+'.s')
            cand=work/'objects'/name
            cache=index.get('files',{}).get(name,{})
            if not s.is_file() or cache.get('s_sha256')!=sha(s):
                raise ValueError('missing SHA-locked MiniC .s: '+name)
            if not cand.is_file():raise ValueError('missing current MiniAS object: '+name)
            f1,f2=elf_flags(frozen/name),elf_flags(cand)
            if f1!=f2 or scanned[name]!=hex(f2):
                raise ValueError('different ABI/ELF flags: '+name)
            if exports(frozen/name)!=exports(cand):
                raise ValueError('global exported symbol mismatch: '+name)
            h=sha(cand)
            if name not in DYNAMIC_HEADERS:
                shutil.copy2(cand,output/name)
                if sha(output/name)!=h:raise ValueError('failed overlay copy: '+name)
                promoted.append((name,h))
                verdict='MINIAS'
            else:
                verdict='FROZEN_GNU_AS_DYNAMIC_HEADER'
            f.write(f'{name}\t{verdict}\t{orig[name]}\t{h}\t{hex(f2)}\n')
    if len(promoted)!=60:
        raise ValueError('expected 60 promoted MiniAS objects; got '+str(len(promoted)))
    for name,h in orig.items():
        if name not in dict(promoted) and sha(output/name)!=h:
            raise ValueError('non-promoted original drifted: '+name)
    with (evidence/'native-object-hashes.sha256').open('w') as out:
        for name,h in promoted: out.write(f'{h}  {name}\n')
    (evidence/'overlay-provenance.json').write_text(json.dumps({
        'schema':'minias-runtime-overlay-v1','cache_identity':a.cache_identity,
        'original_manifest_sha256':sha(manifest),
        'selected_MiniAS_input_count':len(selected),
        'actual_promoted_MiniAS_count':len(promoted),
        'unmodified_original_count':2064-len(promoted),
        'deferred_generated_header_owners':sorted(DYNAMIC_HEADERS),
        'real_linux_runtime':'NOT_YET_RUN',
    },indent=2,sort_keys=True)+'\n')
    print(f'MINIAS_RUNTIME_POOL=PASS scanned={len(selected)} native={len(promoted)} frozen={2064-len(promoted)} original_SHA_attested=2064 C_recompiled=0',flush=True)
    print('MINIAS_RUNTIME_DEFERRED=4 reason=vdso-offsets.h_replay_requires_fresh_MiniC_S_GNU_AS',flush=True)

if __name__=='__main__':
    try:main()
    except Exception as exc:
        print('MINIAS_RUNTIME_POOL=FAIL '+str(exc),file=sys.stderr)
        sys.exit(2)
