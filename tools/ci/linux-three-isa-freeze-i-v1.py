#!/usr/bin/env python3
"""Freeze genuine GNU Kbuild .i once; input SHA makes later MiniC replay cheap."""
import argparse,concurrent.futures,hashlib,json,re,shlex,struct,subprocess,sys
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
    return dict(target=target,profile=j.get("config_profile","broad"),config=j["config_sha256"],image=j["kernel_sha256"],
                source=(e/"linux-source.sha256").read_text().split()[0],
                objects=digest(e/"full-c-owners.txt"),gnu=trip,
                version=subprocess.check_output([gcc,"-dumpfullversion"],text=True).strip())
# Existing GNU .o.cmd is the provenance authority.  The native Kbuild
# command may compile through a temporary output name before copying to .o.
# Preserve its original -o separately; never change the compiler's C input.
MINIC_CFLAGS = {
    "-fshort-wchar", "-fno-short-wchar",
    "-fsigned-char", "-funsigned-char", "-fno-signed-char", "-fno-unsigned-char",
    "-ffunction-sections", "-fno-function-sections",
    "-fPIE", "-fpie", "-fPIC", "-fpic",
    "-fno-pie", "-fno-PIE", "-fno-pic", "-fno-PIC",
}
LOONGARCH_ABI_FLAGS={"-mabi=lp64d","-mabi=lp64s"}
X86_CODEGEN_FLAGS={
    "-fcf-protection=branch","-fcf-protection=none",
    "-mindirect-branch=thunk-extern","-mindirect-branch=keep",
    "-mfunction-return=thunk-extern","-mfunction-return=keep",
    "-fpatchable-function-entry=16,16",
    "-fpatchable-function-entry=0,0","-fpatchable-function-entry=0",
}
def valid_minic_flags(flags,target):
    admitted=MINIC_CFLAGS | (LOONGARCH_ABI_FLAGS if target=="loongarch64" else set()) | (
        X86_CODEGEN_FLAGS if target=="x86_64" else set())
    return isinstance(flags,list) and all(x in admitted for x in flags)
def gnu_assembler_flags(tokens):
    """Replay target machine/ABI and explicit GAS flags from the actual .o.cmd.

    GNU -E freezes C semantics, but cannot encode ELF e_flags or GAS's
    machine model into .i. LoongArch Linux, for example, uses -mabi=lp64s
    even when the distro GNU assembler defaults to lp64d. Never reassemble
    the original target .s using an unrelated host/toolchain default.
    """
    result=[]
    for option in tokens[1:]:
        if option.startswith("-m") and len(option)>2 or option.startswith("-Wa,"):
            result.append(option)
    return result
def valid_assembler_flags(flags):
    return isinstance(flags,list) and all(
        isinstance(flag,str) and len(flag)>2 and
        (flag.startswith("-m") or flag.startswith("-Wa,")) and
        not any(c.isspace() for c in flag)
        for flag in flags)
def original_elf_flags(path,target):
    if target!="loongarch64":return None
    with path.open("rb") as f:hdr=f.read(64)
    if len(hdr)!=64 or hdr[:6]!=b"\x7fELF\x02\x01" or struct.unpack_from("<H",hdr,18)[0]!=258:
        raise ValueError("wrong GNU original LoongArch64 ELF owner: "+str(path))
    abi=struct.unpack_from("<I",hdr,48)[0]&7
    if abi not in (1,2,3):raise ValueError("unknown GNU original LoongArch ABI: "+str(path))
    return abi
def gnu_command_tokens(text,gcc):
    m=re.search(r"^(?:savedcmd|cmd)_[^\n]*\s*:=\s*(.+)$",text,re.M)
    if not m:raise ValueError("no GNU Kbuild command")
    lex=shlex.shlex(m.group(1),posix=True,punctuation_chars=";&|")
    lex.whitespace_split=True
    t=list(lex)
    t=t[:next((i for i,v in enumerate(t) if v in (";","&&","||","|")),len(t))]
    if not t or Path(t[0]).name!=Path(gcc).name or "-c" not in t:
        raise ValueError("not a direct GNU C compile")
    return t
def unqualified_gnu_codegen_flags(tokens,minic_flags):
    """List original codegen/ABI options without a proven MiniC equivalent.

    Preprocessing flags have already affected the frozen GNU .i, warnings
    affect diagnostics only, and -Wa options are replayed with GNU AS.
    Machine, optimization, hardening, debug and C semantic options cannot be
    silently called equivalent merely because the same .i was generated.
    """
    allowed=set(minic_flags)
    return sorted({v for v in tokens[1:] if
                   v.startswith(("-f","-m","-O","-g","-std=")) and
                   v not in allowed and not v.startswith((
                       "-fmacro-prefix-map=", "-ffile-prefix-map="))})
def gnu_argv(text,gcc,obj,out,target=None):
    t=gnu_command_tokens(text,gcc)
    outputs=[t[i+1] for i,v in enumerate(t[:-1]) if v=="-o"]
    if len(outputs)!=1:
        raise ValueError("missing/ambiguous GNU output: "+str(outputs)[:150])
    # The full C-owner inventory is already derived from authentic GNU Kbuild
    # source metadata; a noncanonical temporary -o is not an extra C owner.
    sources=[x for x in t if x.endswith((".c",".i")) and not x.startswith("-")]
    source=re.search(r"^source_[^\n]*\s*:=\s*(\S+\.c)\s*$",text,re.M)
    if len(sources)!=1 and (source is None or source.group(1) not in sources):
        raise ValueError("ambiguous GNU C source: "+str(sources)[:180])
    admitted=MINIC_CFLAGS | (X86_CODEGEN_FLAGS if target=="x86_64" else set())
    flags=[v for v in t if v in admitted]
    assembler_flags=gnu_assembler_flags(t)
    if target=="loongarch64":
        abi_flags=[flag for flag in assembler_flags if flag.startswith("-mabi=")]
        if len(abi_flags)!=1 or abi_flags[0] not in LOONGARCH_ABI_FLAGS:
            raise ValueError("LoongArch Linux C owner must carry one supported GNU Kbuild -mabi")
        # One original Kbuild ABI selection feeds BOTH MiniC call lowering and
        # GNU AS ELF e_flags; changing only the ELF tag is not ABI support.
        flags+=abi_flags
    a=[t[0],"-E","-P"]
    i=1
    while i<len(t):
        v=t[i]
        if v in ("-o","-MF","-MT","-MQ"):
            i+=2
            continue
        if v in ("-c","-MD","-MMD","-MP","-MG") or v.startswith(("-Wp,-MD,","-Wp,-MMD,")):
            i+=1
            continue
        a.append(v)
        i+=1
    return a+["-o",str(out)],flags,assembler_flags,outputs[0]

def verify(e,f,target,gcc):
    j=json.loads((f/"manifest.json").read_text());a=owners(e)
    if j["identity"]!=identity(e,target,gcc) or len(j["rows"])!=len(a):raise ValueError("stale/incomplete .i cache")
    for i,(name,row) in enumerate(zip(a,j["rows"])):
        p=PurePosixPath(row["input"])
        if (name!=row["object"] or row.get("index")!=i or row["shard"]!=i%7
                or p.is_absolute() or ".." in p.parts or p.parts[0]!="i"
                or not valid_minic_flags(row.get("minic_flags"),target)
                or not valid_assembler_flags(row.get("gnu_as_flags"))
                or (target=="loongarch64" and row.get("gnu_elf_abi") not in (1,2,3))):
            raise ValueError("bad .i membership or unsupported C ABI flags")
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
        args,mini_flags,as_flags,gnu_output=gnu_argv(cmd,a.gcc,name,out,a.target)
        original_abi=original_elf_flags(a.out/p,a.target)
        v=subprocess.run(args,cwd=a.out,capture_output=True,timeout=a.timeout)
        if v.returncode or not out.is_file() or out.stat().st_size==0:
            out.unlink(missing_ok=True)
            raise ValueError(v.stderr.decode(errors="replace")[-450:])
        return dict(index=i,object=name,input=relative,sha256=digest(out),
                    shard=i%7,cmd_sha256=hashlib.sha256(cmd.encode()).hexdigest(),
                    minic_flags=mini_flags,gnu_as_flags=as_flags,
                    gnu_elf_abi=original_abi,gnu_output=gnu_output)
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
            v,flags,as_flags,gnu_output=gnu_argv(cmd,"gcc","lib/a.o",Path("lib/a.i"))
            assert v[:3]==["gcc","-E","-P"] and "-c" not in v and v[-2:]==["-o","lib/a.i"]
            assert flags==[] and as_flags==[] and gnu_output=="lib/a.o"
            v,flags,as_flags,gnu_output=gnu_argv(
                cmd.replace(" -c ", " -fshort-wchar -funsigned-char -c ")
                   .replace("-o lib/a.o","-o lib/.temporary-a.o"),
                "gcc","lib/a.o",Path("lib/a.i"))
            assert flags==["-fshort-wchar","-funsigned-char"] and as_flags==[]
            assert gnu_output=="lib/.temporary-a.o"
            v,flags,as_flags,gnu_output=gnu_argv(
                cmd.replace(" -c ", " -mabi=lp64s -mcmodel=normal -msoft-float -Wa,-gdwarf-2 -c "),
                "gcc","lib/a.o",Path("lib/a.i"))
            assert flags==[] and as_flags==[
                "-mabi=lp64s","-mcmodel=normal","-msoft-float","-Wa,-gdwarf-2"]
            v,flags,as_flags,gnu_output=gnu_argv(
                cmd.replace(" -c ", " -mabi=lp64s -msoft-float -c "),
                "gcc","lib/a.o",Path("lib/a.i"),"loongarch64")
            assert flags==["-mabi=lp64s"] and as_flags==["-mabi=lp64s","-msoft-float"]
            assert valid_minic_flags(flags,"loongarch64") and not valid_minic_flags(flags,"x86_64")
            assert valid_assembler_flags(as_flags) and not valid_assembler_flags(["-mabi=lp64s\n-o /tmp/evil"])
            # GNU -E cannot encode C machine code, ABI and hardening options.
            # Preserve them as unqualified evidence rather than silently
            # promoting a same-.i MiniC compile to exact GNU-option parity.
            strict_cmd=cmd.replace(" -c ",
                " -fshort-wchar -fcf-protection=branch -fstack-protector-strong"
                " -ftrivial-auto-var-init=zero -mindirect-branch=thunk-extern"
                " -mfunction-return=thunk-extern -fpatchable-function-entry=16,16"
                " -mcmodel=kernel -O2 -c ")
            _,forwarded,_,_=gnu_argv(strict_cmd,"gcc","lib/a.o",Path("lib/a.i"),"x86_64")
            assert forwarded==["-fshort-wchar","-fcf-protection=branch",
                               "-mindirect-branch=thunk-extern",
                               "-mfunction-return=thunk-extern",
                               "-fpatchable-function-entry=16,16"]
            assert valid_minic_flags(forwarded,"x86_64")
            unqualified=unqualified_gnu_codegen_flags(
                gnu_command_tokens(strict_cmd,"gcc"),forwarded)
            assert unqualified==sorted([
                "-fstack-protector-strong",
                "-ftrivial-auto-var-init=zero",
                "-mcmodel=kernel", "-O2"])
            assert sum(len(list(range(i,99,7))) for i in range(7))==99
            print("GNU_FROZEN_I_SELFTEST=PASS shards=7 replay_flags=1 gnu_machine_abi=1 unqualified_option_audit=1");return 0
        if a.evidence is None or a.frozen is None or a.target is None:p.error("missing required paths/target")
        if a.mode=="freeze":
            if a.out is None:p.error("--out required")
            freeze(a)
        else:verify(a.evidence,a.frozen,a.target,a.gcc)
        return 0
    except (OSError,ValueError,AssertionError,KeyError,subprocess.SubprocessError) as ex:
        print("GNU_FROZEN_I=FAIL "+str(ex),file=sys.stderr);return 2
if __name__=="__main__":sys.exit(main())
