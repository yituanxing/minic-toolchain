#!/usr/bin/env python3
"""Form an isolated, SHA-attested MiniAS overlay for the existing Linux P0/P1 runner.

Consumes ONLY the frozen 2064 MiniC objects and independently cached MiniC .s
+ current MiniAS outputs from minias-linux-corpus-probe-v1.py. Never recompiles
C or alters golden Kbuild reference. Excludes four vDSO-header-sensitive TUs,
which Kbuild legitimately replays against changed generated headers.
"""
import argparse, hashlib, json, pathlib, shutil, struct, subprocess, sys

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

def main():
    p=argparse.ArgumentParser()
    for a in ('original-pool','manifest','scan-root','destination','evidence','cache-identity'):
        p.add_argument('--'+a,required=True)
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
