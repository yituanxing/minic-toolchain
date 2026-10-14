#!/usr/bin/env python3
"""MiniAS C-object coverage probe: SHA-pinned GNU Kbuild commands, cached MiniC .o,
only GCC -E / MiniC -S if .s missing, then MiniAS. No Kbuild, link, or QEMU.
The .s corpus is reusable independently of MiniAS code changes.
"""
from __future__ import annotations
import argparse, hashlib, importlib.util, json, pathlib, struct, subprocess, sys, time

def digest(p):
    h=hashlib.sha256()
    with pathlib.Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''): h.update(b)
    return h.hexdigest()

def execute(cmd,cwd,log,timeout):
    start=time.monotonic()
    try:
        r=subprocess.run(cmd,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
        rc,out=r.returncode,r.stdout
    except subprocess.TimeoutExpired as e:
        rc,out=124,(e.stdout or b'')+b'\nTIMEOUT'
    log.parent.mkdir(parents=True,exist_ok=True)
    log.write_bytes(out[-8192:])
    return rc,round((time.monotonic()-start)*1000)

def flags(p):
    b=p.read_bytes()[:52]
    if len(b)!=52 or b[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<HH',b,16)!=(1,243):
        raise RuntimeError('not ELF64 LE RISC-V relocatable: '+str(p))
    f=struct.unpack_from('<I',b,48)[0]
    if f&~7: raise RuntimeError('unsupported kernel ELF flags: '+str(p))
    return f

def clean_gcc(args):
    out=[]; i=0
    while i<len(args):
        a=args[i]
        if a in ('-c','-MD','-MMD','-MP') or a.startswith(('-Wp,-MMD,','-Wp,-MD,')):
            i+=1;continue
        if a in ('-o','-MF','-MT','-MQ'):
            if i+1==len(args): raise RuntimeError('truncated option '+a)
            i+=2;continue
        out.append(a);i+=1
    if sum(x.endswith('.c') for x in out)!=1: raise RuntimeError('missing unique C source')
    return out

def globals_in(path):
    r=subprocess.run(['riscv64-linux-gnu-nm','-g','--defined-only','-P',str(path)],
                     capture_output=True,text=True,check=True)
    return {s.split(' ',1)[0] for s in r.stdout.splitlines() if s}

def main():
    p=argparse.ArgumentParser()
    for n in ('golden-out','candidate-pool','manifest','minic','minias','work','cache-identity'):
        p.add_argument('--'+n,required=True)
    p.add_argument('--limit',type=int,default=12)
    a=p.parse_args()
    if not 1<=a.limit<=2064 or not a.cache_identity.startswith('gnu-minic-objects-v1-'):
        raise RuntimeError('invalid scope or untrusted cache identity')
    golden=pathlib.Path(a.golden_out).resolve()
    pool=pathlib.Path(a.candidate_pool).resolve()
    manifest=pathlib.Path(a.manifest).resolve()
    minic=pathlib.Path(a.minic).resolve()
    minias=pathlib.Path(a.minias).resolve()
    work=pathlib.Path(a.work).resolve()
    work.mkdir(parents=True,exist_ok=True)
    records=[line.split(maxsplit=1) for line in manifest.read_text().splitlines() if line.strip()]
    if len(records)!=2064 or len(set(r[1] for r in records))!=2064:
        raise RuntimeError('not exact 2064-object MiniC manifest')
    for want,rel in records:
        if not rel.startswith('minic/') or digest(pool/rel.removeprefix('minic/'))!=want:
            raise RuntimeError('original MiniC cache mismatch: '+rel)
    names=[r[1].removeprefix('minic/') for r in records]
    prefixes=['drivers/tty/serial/earlycon.o','init/','kernel/','mm/','fs/','net/',
              'lib/','arch/','block/','security/','drivers/','crypto/','ipc/','sound/']
    selected=[]
    for prefix in prefixes:
        poss=sorted(n for n in names if n not in selected and
                    (n==prefix if prefix.endswith('.o') else n.startswith(prefix)))
        if poss: selected.append(poss[0])
        if len(selected)>=a.limit: break
    selected+=sorted(n for n in names if n not in selected)[:max(0,a.limit-len(selected))]
    schema={'schema':'minias-linux-s-corpus-v1','identity':a.cache_identity,
            'minic_sha256':digest(minic),'manifest_sha256':digest(manifest),
            'config_sha256':digest(golden/'.config'),'selected':selected}
    sdir=work/'assembly';index=sdir/'index.json'
    old=json.loads(index.read_text()) if index.is_file() else None
    if old:
        # A wider corpus must reuse the previously certified .s rather than
        # rerun even the GCC-E / MiniC-S stages for already scanned objects.
        if any(old.get(k)!=v for k,v in schema.items() if k!='selected'):
            raise RuntimeError('cached assembly belongs to different MiniC/Kbuild profile')
        if not set(old.get('selected',[])).issubset(set(selected)):
            raise RuntimeError('cached assembly selection not subset of new expansion')
    files={} if old is None else old.get('files',{})
    if set(files)-set(selected): raise RuntimeError('extra cached assembly members')
    module=pathlib.Path(__file__).with_name('linux-runtime-kbuild-regeneration-guard-v1.py')
    spec=importlib.util.spec_from_file_location('kbuild_guard',module)
    guard=importlib.util.module_from_spec(spec);sys.modules[spec.name]=guard;spec.loader.exec_module(guard)
    result=[]
    for owner in selected:
        stem=owner[:-2]; obj=pool/owner
        s=sdir/(stem+'.s'); s.parent.mkdir(parents=True,exist_ok=True)
        cmd=golden/pathlib.Path(owner).parent/('.'+pathlib.Path(owner).name+'.cmd')
        args=guard.pinned_kbuild_cc_args(golden,owner)
        cmd_sha=digest(cmd); o_sha=digest(obj)
        prev=files.get(owner)
        if prev:
            if not s.is_file() or (prev['cmd_sha256'],prev['original_sha256'],prev['s_sha256'])!=(cmd_sha,o_sha,digest(s)):
                raise RuntimeError('stale .s cache: '+owner)
            print('MINIAS_CORPUS_S=HIT owner='+owner,flush=True)
        else:
            tmp=work/'scratch'/(stem+'.i');tmp.parent.mkdir(parents=True,exist_ok=True)
            rc,ms=execute(['/usr/bin/riscv64-linux-gnu-gcc','-E','-P',*clean_gcc(args),'-o',str(tmp)],
                          golden,work/'evidence'/(stem+'.gcc-E.log'),120)
            if rc or not tmp.is_file(): raise RuntimeError('pinned GCC-E failed: '+owner)
            rc2,ms2=execute([str(minic),'-S',str(tmp),'-o',str(s)],
                            golden,work/'evidence'/(stem+'.minic-S.log'),180)
            tmp.unlink(missing_ok=True)
            if rc2 or not s.is_file(): raise RuntimeError('pinned MiniC-S failed: '+owner)
            files[owner]={'cmd_sha256':cmd_sha,'original_sha256':o_sha,'s_sha256':digest(s)}
            print(f'MINIAS_CORPUS_S=GENERATED owner={owner} gcc_E_ms={ms} minic_S_ms={ms2}',flush=True)
        # The accepted run #38491622247 independently captured this exact
        # real Linux earlycon assembly. Reject different compiler input.
        if owner=='drivers/tty/serial/earlycon.o' and digest(s)!='5a2d6035541018dbacbcc8b7a838f24239d934fd3c3b7fdb3e414e23ddacc02b':
            raise RuntimeError('earlycon frozen .s identity mismatch')
        f=flags(obj); arch='rv64gc' if f&1 else 'rv64g'; abi={0:'lp64',2:'lp64f',4:'lp64d',6:'lp64q'}[f&6]
        dst=work/'objects'/owner;dst.parent.mkdir(parents=True,exist_ok=True)
        rc,ms=execute([str(minias),'-march='+arch,'-mabi='+abi,'-o',str(dst),str(s)],
                      golden,work/'evidence'/(stem+'.minias.log'),75)
        status='ASSEMBLE_ERROR'
        if rc==0:
            try:
                if flags(dst)!=f: status='ELF_FLAGS_MISMATCH'
                elif globals_in(dst)!=globals_in(obj): status='GLOBAL_EXPORT_MISMATCH'
                else: status='OBJECT_BASIC_PASS'
            except Exception: status='OBJECT_VALIDATION_ERROR'
        result.append((owner,status,rc,ms,hex(f)))
        print(f'MINIAS_CORPUS_AS owner={owner} status={status} rc={rc} elapsed_ms={ms}',flush=True)
    index.write_text(json.dumps({**schema,'files':files},indent=2,sort_keys=True)+'\n')
    with (work/'scan.tsv').open('w') as o:
        o.write('owner\tstatus\trc\tms\te_flags\n')
        for row in result:o.write('\t'.join(map(str,row))+'\n')
    pass_count=sum(x[1]=='OBJECT_BASIC_PASS' for x in result)
    print(f'MINIAS_CORPUS_SCAN=COMPLETE objects={len(result)} basic_pass={pass_count} gaps={len(result)-pass_count} C_recompiled=0 Kbuild=NO link=NO QEMU=NO',flush=True)

if __name__=='__main__':
    try: main()
    except Exception as e:
        print('MINIAS_CORPUS_SCAN=INCONCLUSIVE '+str(e),file=sys.stderr)
        sys.exit(2)
