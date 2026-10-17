#!/usr/bin/env python3
"""Use CERTIFIED RV64 Stage2 MiniC to build the *other* current toolchain tools.

The full source lists are read from Makefile, with explicit linked minielf
dependencies (never a historical 46-file bootstrap manifest). Each C TU is
preprocessed once by pinned GNU GCC, then both current host Stage0 MiniC and
certified QEMU Stage2 compile that SAME .i. Require byte-identical .s.
GNU RV64 AS/link/CRT/libc are retained intentionally: this gate proves
compiler self-hosting across the toolchain C source, NOT GNU-free toolchain.
Actual QEMU runtime tests then exercise Stage2-built MiniPP/MiniAS.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
ELF_READER="elf/src/reader.c"
ELF_REL="elf/src/relocatable_writer.c"
ELF_BIN="elf/src/binary_export.c"
ELF_RW="elf/src/rewrite.c"
ARCHIVE="archiver/src/archive.c"
PROFILES={
    "minic-cpp":("MINIPP_SOURCES",[],["preprocessor/include","preprocessor/src","preprocessor/target"]),
    "minic-as":("MINIAS_SOURCES",[ELF_READER,ELF_REL],["assembler/src","elf/include"]),
    "minic-ar":("MINIAR_SOURCES",[ELF_READER],["archiver/include","elf/include"]),
    "minic-ld":("MINILD_SOURCES",[ELF_READER,ELF_REL],["linker/include","linker/src","elf/include"]),
    "minic-objcopy":("MINIOBJCOPY_SOURCES",[ELF_READER,ELF_BIN,ELF_RW],["elf/include"]),
    "minic-nm":("MININM_SOURCES",[ELF_READER,ARCHIVE],["archiver/include","elf/include"]),
    "minic-strip":("MINISTRIP_SOURCES",[ELF_READER,ELF_RW],["elf/include"]),
    "minic":(None,[],["include","src"]),
}
CANONICAL=("minic-cpp","minic-as","minic-ar","minic-ld","minic-objcopy","minic-nm","minic-strip","minic")

def sha(path):
    h=hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda:f.read(1024*1024),b""):
            h.update(block)
    return h.hexdigest()

def execute(argv, *, env=None, timeout=240, log=None, check=True):
    start=time.monotonic()
    try:
        p=subprocess.run(list(map(str,argv)),cwd=ROOT,env=env,
                         stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout)
    except subprocess.TimeoutExpired:
        raise RuntimeError("TIMEOUT args="+repr(list(map(str,argv[:4])))) from None
    if log is not None:
        Path(log).parent.mkdir(parents=True,exist_ok=True)
        Path(log).write_bytes(p.stdout+b"\n-- STDERR --\n"+p.stderr)
    if check and p.returncode!=0:
        raise RuntimeError("rc=%d cmd=%s stderr=%s"%(p.returncode,
                " ".join(map(str,argv[:6])),p.stderr[-1200:].decode(errors="replace")))
    return p,round(time.monotonic()-start,3)

def source_group(makefile,variable):
    m=re.search(r"(?ms)^"+variable+r"\s*:=\s*\\\s*\n(.*?)(?=^\w+_OBJECTS\s*:)",makefile)
    if not m:raise RuntimeError("Makefile source group missing "+variable)
    sources=[r.strip().rstrip("\\").strip() for r in m.group(1).splitlines() if r.strip()]
    if not sources or len(sources)!=len(set(sources)):raise RuntimeError("invalid "+variable)
    return sources

def source_plan(requested):
    mf=(ROOT/"Makefile").read_text()
    plan={}
    order=[]
    for tool in requested:
        variable,elf,include=PROFILES[tool]
        # Makefile explicitly places ELF .o BEFORE the target-specific units.
        sources=elf+(source_group(mf,variable) if variable else ["tools/minic/main.c"])
        if len(sources)!=len(set(sources)):
            raise RuntimeError("duplicate link C source "+tool)
        if (variable and not sources[-1].endswith("/main.c")):
            raise RuntimeError("unexpected CLI entry "+tool)
        for src in sources:
            if not (ROOT/src).is_file():raise RuntimeError("missing "+src)
            if src not in plan:
                order.append(src)
                plan[src]={"consumer":tool,"include":include}
            else:
                # Source may be shared by multiple tools. Its .i is still
                # prepared just once with a conservative union of includes.
                plan[src]["include"]=list(dict.fromkeys(plan[src]["include"]+include))
        plan["#"+tool]={"sources":sources}
    return plan,order

def main():
    cli=argparse.ArgumentParser()
    cli.add_argument("--stage0",required=True,type=Path)
    cli.add_argument("--stage2",required=True,type=Path)
    cli.add_argument("--out",required=True,type=Path)
    cli.add_argument("--workers",type=int,choices=(1,2,4),default=4)
    cli.add_argument("--tools",default=",".join(CANONICAL))
    args=cli.parse_args()
    tools=args.tools.split(",")
    if len(tools)!=len(set(tools)) or any(x not in PROFILES for x in tools):
        raise RuntimeError("invalid selected tool names")
    stage0=args.stage0.resolve(); stage2=args.stage2.resolve()
    if not stage0.is_file() or not stage2.is_file():raise RuntimeError("missing Stage0/2")
    args.out.mkdir(parents=True,exist_ok=True);out=args.out.resolve()
    for p in ("i","s/stage0","s/stage2","bin","log","cases"):
        (out/p).mkdir(parents=True,exist_ok=True)
    plan,items=source_plan(tools)
    env=os.environ.copy()
    env.update(MINIC_LINUX_FULL_CROSSBLOCK="1",MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ="1")
    print("BOOTSTRAP_TOOLCHAIN_PLAN=PASS tools=%d unique_tus=%d stage0_sha=%s stage2_sha=%s"%
          (len(tools),len(items),sha(stage0),sha(stage2)),flush=True)
    rows=[]
    def compile_one(src):
        key=src.replace("/","__").replace(".","_")
        pre=out/"i"/(key+".i")
        old=out/"s/stage0"/(key+".s")
        new=out/"s/stage2"/(key+".s")
        log=out/"log"/(key+".log")
        inc=["include","src","elf/include","archiver/include",
             "assembler/src","preprocessor/include","preprocessor/src",
             "preprocessor/target","linker/include","linker/src"]
        # Every command consumes an identical .i. Extra include paths are
        # deliberately stable, and source identity is preserved separately.
        argv=["riscv64-linux-gnu-gcc","-E","-P","-std=c11","-DNDEBUG",*["-I"+p for p in inc],
              str(ROOT/src),"-o",str(pre)]
        execute(argv,env=env,timeout=160,log=log)
        if pre.stat().st_size==0:raise RuntimeError("empty GNU .i "+src)
        _,t0=execute([stage0,"-S",pre,"-o",old],env=env,timeout=360,log=log.with_suffix(".stage0.log"))
        _,t2=execute(["qemu-riscv64",stage2,"-S",pre,"-o",new],env=env,
                      timeout=720,log=log.with_suffix(".stage2.log"))
        if not old.is_file() or not new.is_file() or not old.stat().st_size or sha(old)!=sha(new):
            raise RuntimeError("STAGE0_STAGE2_ASM_MISMATCH source="+src+
                               " old="+(sha(old) if old.is_file() else "MISSING")+
                               " stage2="+(sha(new) if new.is_file() else "MISSING"))
        return {"source":src,"input_sha256":sha(pre),"asm_sha256":sha(new),
                "input_bytes":pre.stat().st_size,"assembly_bytes":new.stat().st_size,
                "stage0_s":t0,"stage2_s":t2}
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        pending={pool.submit(compile_one,src):src for src in items}
        for done in as_completed(pending):
            try:row=done.result()
            except Exception as exc:
                raise RuntimeError("BOOTSTRAP_TOOLCHAIN_TU=FAIL source="+pending[done]+" "+str(exc)) from None
            rows.append(row)
            print("BOOTSTRAP_TOOLCHAIN_TU=PASS %d/%d source=%s stage2_sec=%.3f"%
                  (len(rows),len(items),row["source"],row["stage2_s"]),flush=True)
    result={"selected_tools":tools,"stage0_sha256":sha(stage0),"stage2_sha256":sha(stage2),
            "tus":sorted(rows,key=lambda x:items.index(x["source"])),
            "binaries":{}}
    for name in tools:
        files=plan["#"+name]["sources"]
        outs=[out/"s/stage2"/(s.replace("/","__").replace(".","_")+".s") for s in files]
        elf=out/"bin"/name
        _,secs=execute(["riscv64-linux-gnu-gcc","-static",*outs,"-o",elf],env=env,
                       timeout=450,log=out/"log"/(name+"-link.log"))
        kind,_=execute(["riscv64-linux-gnu-readelf","-h",elf],env=env,
                       log=out/"log"/(name+"-readelf.log"))
        if b"RISC-V" not in kind.stdout:raise RuntimeError("not a real RISC-V ELF "+name)
        result["binaries"][name]={"sha256":sha(elf),"bytes":elf.stat().st_size,
                                  "linked_sources":len(files),"link_sec":secs}
        print("BOOTSTRAP_TOOLCHAIN_LINK=PASS tool=%s linked_tus=%d elf_sha256=%s"%
              (name,len(files),sha(elf)),flush=True)
        # Real RV64 execution, not only ELF inspection.
        p,_=execute(["qemu-riscv64",elf,"--help"],env=env,timeout=60,check=False,
                    log=out/"log"/(name+"-help.log"))
        if p.returncode in (-11,139,-6,134):
            raise RuntimeError("Stage2-built tool crashed in QEMU "+name)
        result["binaries"][name]["help_rc"]=p.returncode
    if "minic-cpp" in tools:
        source=out/"cases"/"pp.c";source.write_text("#define VALUE 42\nint answer = VALUE;\n")
        target=out/"cases"/"pp.stage2.i"
        argv=["qemu-riscv64",out/"bin/minic-cpp","-E","-P","-undef","-nostdinc",
              "-o",target,source]
        execute(argv,env=env,timeout=90,log=out/"log/pp-real.log")
        if "int answer = 42;" not in target.read_text():
            raise RuntimeError("Stage2-built MiniPP wrong macro result")
        print("BOOTSTRAP_TOOLCHAIN_MINIPP_RUNTIME=PASS value=42",flush=True)
    if "minic-as" in tools:
        source=out/"cases"/"as.s"
        source.write_text(".text\n.globl main\n.type main, @function\nmain:\n  li a0, 42\n  ret\n.size main, .-main\n")
        obj=out/"cases"/"as.stage2.o"
        execute(["qemu-riscv64",out/"bin/minic-as","-o",obj,source],env=env,
                timeout=90,log=out/"log/as-real.log")
        e,_=execute(["riscv64-linux-gnu-readelf","-h",obj],env=env)
        if b"REL (Relocatable file)" not in e.stdout or b"RISC-V" not in e.stdout:
            raise RuntimeError("Stage2-built MiniAS did not emit RV64 ET_REL")
        print("BOOTSTRAP_TOOLCHAIN_MINIAS_RUNTIME=PASS RV64_ET_REL=true",flush=True)
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("BOOTSTRAP_STAGE2_TOOLCHAIN=PASS tools=%d unique_tus=%d parity=%d/%d linked=%d "
          "minipp=%s minias=%s GNU_PP_AS_LD_CRT_STILL_REQUIRED=true"%
          (len(tools),len(items),len(rows),len(items),len(result["binaries"]),
           "minic-cpp" in tools,"minic-as" in tools),flush=True)

if __name__=="__main__":
    try:main()
    except (RuntimeError,OSError) as e:
        print("BOOTSTRAP_STAGE2_TOOLCHAIN=FAIL "+str(e),file=sys.stderr)
        sys.exit(1)
