#!/usr/bin/env python3
"""Freeze genuine GNU Kbuild .i once; input SHA makes later MiniC replay cheap."""
import argparse,concurrent.futures,hashlib,json,re,shlex,subprocess,sys
from pathlib import Path,PurePosixPath
def digest(p):
    h=hashlib.sha256()
    with p.open("rb") as f:
        for buf in iter(lambda:f.read(1048576),b""):h.update(buf)
    return h.hexdigest()
def owners(e):
    a=[x.strip() for x in (e/"full-c-owners.txt").read_text().splitlines() if x.strip() and not x.startswith("#")]
    if not a or len(a)!=len(set(a)):raise ValueError("invalid owner manifest")
    for x in a:
        p=PurePosixPath(x)
        if p.is_absolute() or ".." in p.parts or len(p.parts)<2 or p.suffix!=".o":raise ValueError("unsafe C owner")
    return a
def identity(e,target,gcc):
    j=json.loads((e/"discovery.json").read_text())
    if j["target"]!=target or j["verdict"]!="GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC":raise ValueError("not original GNU")
    trip=subprocess.check_output([gcc,"-dumpmachine"],text=True).strip()
    if not trip.startswith({"x86_64":"x86_64","aarch64":"aarch64","loongarch64":"loongarch64"}[target]):raise ValueError("GNU target mismatch")
    return dict(target=target,config=j["config_sha256"],image=j["kernel_sha256"],
                source=(e/"linux-source.sha256").read_text().split()[0],
                objects=digest(e/"full-c-owners.txt"),gnu=trip,
                version=subprocess.check_output([gcc,"-dumpfullversion"],text=True).strip())
def gnu_argv(text,gcc,obj,out):
    m=re.search(r"^(?:savedcmd|cmd)_[^\n]*\s*:=\s*(.+)$",text,re.M)
    if not m:raise ValueError("no GNU Kbuild command")
    lex=shlex.shlex(m.group(1),posix=True,punctuation_chars=";&|")
    lex.whitespace_split=True;t=list(lex)
    t=t[:next((i for i,v in enumerate(t) if v in (";","&&","||","|")),len(t))]
    if not t or Path(t[0]).name!=Path(gcc).name or "-c" not in t:raise ValueError("not GNU C compile")
    if "-o" not in t or t.index("-o")+1>=len(t) or t[t.index("-o")+1]!=obj:raise ValueError("output differs")
    if sum(v.endswith(".c") and not v.startswith("-") for v in t)!=1:raise ValueError("C source mismatch")
    a=[t[0],"-E","-P"];i=1
    while i<len(t):
        v=t[i]
        if v in ("-o","-MF","-MT","-MQ"):i+=2;continue
        if v in ("-c","-MD","-MMD","-MP","-MG") or v.startswith(("-Wp,-MD,","-Wp,-MMD,")):i+=1;continue
        a.append(v);i+=1
    return a+["-o",str(out)]
def verify(e,f,target,gcc):
    j=json.loads((f/"manifest.json").read_text());a=owners(e)
    if j["identity"]!=identity(e,target,gcc) or len(j["rows"])!=len(a):raise ValueError("stale/incomplete .i cache")
    for i,(name,row) in enumerate(zip(a,j["rows"])):
        p=PurePosixPath(row["input"])
        if name!=row["object"] or row["shard"]!=i%7 or p.is_absolute() or ".." in p.parts or p.parts[0]!="i":raise ValueError("bad .i membership")
        file=f/p
        if file.is_symlink() or digest(file)!=row["sha256"]:raise ValueError("wrong cached .i SHA "+name)
    print(f"GNU_FROZEN_I_CACHE=PASS target={target} owners={len(a)} shards=7 GCC_reruns=0")
def freeze(a):
    items=owners(a.evidence);idn=identity(a.evidence,a.target,a.gcc)
    if a.frozen.exists() and list(a.frozen.iterdir()):raise ValueError("refuse overwrite cached source")
    a.frozen.mkdir(parents=True,exist_ok=True)
    def one(pair):
        i,name=pair;p=PurePosixPath(name)
        cmd=(a.out/p.parent/("."+p.name+".cmd")).read_text()
        relative="i/"+str(p.with_suffix(".i"));out=a.frozen/relative
        out.parent.mkdir(parents=True,exist_ok=True)
        args=gnu_argv(cmd,a.gcc,name,out)
        v=subprocess.run(args,cwd=a.out,capture_output=True,timeout=a.timeout)
        if v.returncode or not out.is_file() or out.stat().st_size==0:
            out.unlink(missing_ok=True)
            raise ValueError(v.stderr.decode(errors="replace")[-450:])
        return dict(object=name,input=relative,sha256=digest(out),shard=i%7,cmd_sha256=hashlib.sha256(cmd.encode()).hexdigest())
    results={};errors=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as pool:
        work={pool.submit(one,pair):pair[1] for pair in enumerate(items)}
        for f in concurrent.futures.as_completed(work):
            try:results[work[f]]=f.result()
            except Exception as exc:errors.append((work[f],str(exc)))
    j=dict(schema="three-isa-gnu-i-v1",identity=idn,
           rows=[results[x] for x in items if x in results],errors=errors)
    (a.frozen/"manifest.json").write_text(json.dumps(j,indent=2)+"\n")
    print(f"GNU_FROZEN_I_FREEZE target={a.target} success={len(results)}/{len(items)} failed={len(errors)}")
    for name,error in errors[:20]:print(f"GNU_I_FAIL {name} {error[:250]}",file=sys.stderr)
    if errors:raise ValueError("incomplete GNU -E; artifacts preserve failures")
    verify(a.evidence,a.frozen,a.target,a.gcc)
def main():
    p=argparse.ArgumentParser()
    p.add_argument("mode",choices=["freeze","verify","selftest"])
    p.add_argument("--out",type=Path);p.add_argument("--evidence",type=Path)
    p.add_argument("--frozen",type=Path);p.add_argument("--gcc",default="gcc")
    p.add_argument("--target",choices=["x86_64","aarch64","loongarch64"])
    p.add_argument("--workers",type=int,default=7);p.add_argument("--timeout",type=int,default=90)
    a=p.parse_args()
    try:
        if a.mode=="selftest":
            cmd="savedcmd_lib/a.o := gcc -Wp,-MMD,lib/.a.o.d -c -o lib/a.o lib/a.c ; tools/objtool a.o"
            v=gnu_argv(cmd,"gcc","lib/a.o",Path("lib/a.i"))
            assert v[:3]==["gcc","-E","-P"] and "-c" not in v and v[-2:]==["-o","lib/a.i"]
            assert sum(len(list(range(i,99,7))) for i in range(7))==99
            print("GNU_FROZEN_I_SELFTEST=PASS shards=7 replay_flags=1");return 0
        if a.evidence is None or a.frozen is None or a.target is None:p.error("missing required paths/target")
        if a.mode=="freeze":
            if a.out is None:p.error("--out required")
            freeze(a)
        else:verify(a.evidence,a.frozen,a.target,a.gcc)
        return 0
    except (OSError,ValueError,AssertionError,KeyError,subprocess.SubprocessError) as ex:
        print("GNU_FROZEN_I=FAIL "+str(ex),file=sys.stderr);return 2
if __name__=="__main__":sys.exit(main())
