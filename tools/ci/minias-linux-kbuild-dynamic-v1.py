#!/usr/bin/env python3
"""Replace ONLY Kbuild's four changed-vDSO-header MiniC+GNU-AS .o with MiniAS.

Run immediately after the existing exact GNU-E / MiniC-S / GNU-AS replay, before
the second Kbuild link. Consume those four fresh MiniC .s, never stale cached S.
GNU object's exact ELF e_flags and global symbols are the per-TU ABI oracle.
"""
import argparse,hashlib,pathlib,struct,subprocess,sys

DYNAMIC={
 'arch/riscv/kernel/alternative.o',
 'arch/riscv/kernel/signal.o',
 'arch/riscv/kernel/compat_signal.o',
 'arch/riscv/kernel/vdso.o',
}

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()

def flags(path):
    b=path.read_bytes()[:52]
    if len(b)!=52 or b[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<HH',b,16)!=(1,243):
        raise ValueError('not RV64 ELF64LE relocatable object: '+str(path))
    flags=struct.unpack_from('<I',b,48)[0]
    if flags&~7:raise ValueError('unexpected RISC-V ABI ELF bits '+hex(flags))
    return flags

def exports(path):
    r=subprocess.run(['riscv64-linux-gnu-nm','-g','--defined-only','-P',str(path)],check=True,capture_output=True,text=True)
    return {s.split(' ',1)[0] for s in r.stdout.splitlines() if s.strip()}

def main():
    p=argparse.ArgumentParser()
    for name in ('out','affected','minias','manifest','log-dir'):p.add_argument('--'+name,required=True)
    a=p.parse_args()
    out=pathlib.Path(a.out).resolve()
    affected=[x.strip() for x in pathlib.Path(a.affected).read_text().splitlines() if x.strip()]
    if set(affected)!=DYNAMIC or len(affected)!=4:raise ValueError('generated-header owner set drifted: '+str(affected))
    minias=pathlib.Path(a.minias).resolve()
    if not minias.is_file():raise ValueError('MiniAS binary missing')
    log=pathlib.Path(a.log_dir).resolve();log.mkdir(parents=True,exist_ok=True)
    manifest=pathlib.Path(a.manifest).resolve()
    hashes=[]
    for target in sorted(affected):
        source=out/(target[:-2]+'.minic-stage2.s')
        output=out/target
        tmp=output.with_name(output.name+'.minias-new')
        if not source.is_file() or not source.stat().st_size:raise ValueError('current-header MiniC S missing: '+target)
        frozen=flags(output)
        march='rv64gc' if frozen&1 else 'rv64g'
        mabi={0:'lp64',2:'lp64f',4:'lp64d',6:'lp64q'}[frozen&6]
        tmp.unlink(missing_ok=True)
        try:
            r=subprocess.run([str(minias),'-march='+march,'-mabi='+mabi,'-o',str(tmp),str(source)],
                             capture_output=True,text=True,timeout=90)
            (log/(target.replace('/','_')+'.minias.log')).write_text(r.stdout[-20000:]+r.stderr[-20000:])
            if r.returncode:raise ValueError(f'MiniAS dynamic assembly failed: {target}, exit={r.returncode}')
            if flags(tmp)!=frozen or exports(tmp)!=exports(output):
                raise ValueError('dynamic MiniAS ABI or global symbol mismatch: '+target)
            h=sha(tmp)
            tmp.replace(output)
            if sha(output)!=h:raise ValueError('dynamic MiniAS replacement changed: '+target)
            hashes.append((h,target))
            print(f'MINIAS_DYNAMIC_OWNER=PASS target={target} ABI={hex(frozen)} native_sha={h}',flush=True)
        finally:
            tmp.unlink(missing_ok=True)
    manifest.parent.mkdir(parents=True,exist_ok=True)
    with manifest.open('w') as f:
        for h,n in hashes:f.write(f'{h}  {n}\n')
    print('MINIAS_DYNAMIC_RECONCILE=PASS native=4 original_GNU_AS_objects_replaced=true current_header_MiniC_S_reused=true',flush=True)

if __name__=='__main__':
    try:main()
    except Exception as e:
        print('MINIAS_DYNAMIC_RECONCILE=FAIL '+str(e),file=sys.stderr)
        sys.exit(2)
