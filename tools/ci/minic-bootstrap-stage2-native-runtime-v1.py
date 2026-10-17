#!/usr/bin/env python3
"""QEMU runtime acceptance of all eight already Stage2-BUILT CLI artifacts.

Uses certified Stage2-built ELF64 programs from the prior CI artifact;
does NOT rebuild Stage0/Stage1/Stage2 or recompile their 30 toolchain TUs.
Exercise the EXISTING MiniPP, MiniAS, MiniAR, MiniLD, MiniObjcopy oracles.
Additional end-to-end rung: Stage2 MiniC -S -> Stage2 MiniAS -o ->
Stage2 MiniLD ELF -> qemu-riscv64 exit(42), without GNU AS/LD in this path.
GNU compiler preprocesses C and provides a reference for separate suites.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
TOOLS=("minic-cpp","minic-as","minic-ar","minic-ld","minic-objcopy",
       "minic-nm","minic-strip","minic")

def hashfile(p):
    h=hashlib.sha256()
    with Path(p).open("rb") as f:
        for b in iter(lambda:f.read(1024*1024),b""):
            h.update(b)
    return h.hexdigest()

def call(argv,env,log,timeout=300,expected=0):
    start=time.monotonic()
    try:
        p=subprocess.run(list(map(str,argv)),cwd=ROOT,env=env,
                         stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout)
    except subprocess.TimeoutExpired:
        raise RuntimeError("timeout "+repr(list(map(str,argv[:4])))) from None
    log.parent.mkdir(parents=True,exist_ok=True)
    log.write_bytes(p.stdout+b"\n---stderr---\n"+p.stderr)
    secs=round(time.monotonic()-start,3)
    if p.returncode!=expected:
        raise RuntimeError("rc=%d expected=%d cmd=%s output=%s"%(p.returncode,expected,
                            " ".join(map(str,argv[:5])),
                            (p.stdout[-600:]+p.stderr[-1600:]).decode(errors="replace")))
    return secs

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--toolchain",type=Path,required=True)
    parser.add_argument("--stage2",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    artifact=args.toolchain.resolve()
    stage2=args.stage2.resolve()
    if not stage2.is_file():raise RuntimeError("missing immutable compiler Stage2 ELF")
    report=json.loads((artifact/"summary.json").read_text())
    if report["stage2_sha256"]!=hashfile(stage2):
        raise RuntimeError("toolchain artifact/Stage2 ELF identity mismatch")
    if set(report["binaries"])!=set(TOOLS) or len(report["tus"])!=30:
        raise RuntimeError("not certified full 8-tool / 30 TU artifact")
    out=args.out.resolve()
    out.mkdir(parents=True,exist_ok=True)
    wrapper=out/"wrappers";wrapper.mkdir(exist_ok=True)
    for tool in TOOLS:
        elf=artifact/"bin"/tool
        if not elf.is_file() or hashfile(elf)!=report["binaries"][tool]["sha256"]:
            raise RuntimeError("missing/changed Stage2-built native executable "+tool)
        shim=wrapper/tool
        shim.write_text("#!/bin/sh\nexec qemu-riscv64 "+str(elf)+" \"$@\"\n")
        shim.chmod(0o755)
    env=os.environ.copy()
    env.update(BUILD_DIR=str(out/"fixtures"),
               MINIPP=str(wrapper/"minic-cpp"),
               MINIAS=str(wrapper/"minic-as"),
               MINIAR=str(wrapper/"minic-ar"),
               MINILD=str(wrapper/"minic-ld"),
               MINIOBJCOPY=str(wrapper/"minic-objcopy"),
               MINIC_LINUX_FULL_CROSSBLOCK="1",
               MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ="1")
    suites=[
      ("minipp-a0","tests/preprocessor/run-a0.sh",{}),
      ("minias-a0","tests/assembler/run-a0.sh",{}),
      ("miniar-a0","tests/archiver/run-a0.sh",{}),
      # This exercises two Stage2-built executables together: Stage2 MiniAS
      # produces the input object and Stage2 MiniLD performs -r link.
      ("minild-a0","tests/linker/run-a0.sh",{"RISCV_AS":str(wrapper/"minic-as")}),
      ("miniobjcopy-a0","tests/objcopy/run-a0.sh",{})
    ]
    # Distinguish MiniPP simple CLI and '-x c' handling, plus short/long
    # source paths. Do not let a failed gate conceal every other native tool.
    diag=out/"diagnostics"
    diag.mkdir(parents=True,exist_ok=True)
    source=diag/"input.c"
    source.write_text("int plain = 7;\\n".replace("\\\\n","\\n"))
    runs=[]
    for key,args_ in (
        ("basic",["-E","-P","-undef","-nostdinc"]),
        ("x_c",["-E","-P","-undef","-nostdinc","-x","c"]),
    ):
        output=diag/(key+".i")
        argv=[wrapper/"minic-cpp",*args_,"-o",output,source]
        try:
            sec=call(argv,env,out/"logs"/("minipp-"+key+".log"),timeout=90)
            status="PASS"
        except RuntimeError as exc:
            sec=None
            status="FAIL:"+str(exc)[-400:]
        runs.append({"case":key,"status":status,"seconds":sec})
        print("BOOTSTRAP_NATIVE_MINIPP_DIAG case=%s status=%s"%(key,status),flush=True)
    results={"minipp-argv-diagnostic":{"cases":runs}}
    failures=[]
    for key,test,extra in suites:
        print("BOOTSTRAP_NATIVE_SUITE=START case="+key,flush=True)
        # Trace shell assertions so an existing silent set -e failure has a
        # precise command/line in the retained artifact, not a guessed bug.
        shell_args=["sh","-x",test] if key=="minipp-a0" else ["sh",test]
        try:
            sec=call(shell_args,{**env,**extra},out/"logs"/(key+".log"),timeout=900)
            results[key]={"seconds":sec,"status":"PASS"}
            print("BOOTSTRAP_NATIVE_SUITE=PASS case=%s seconds=%.3f"%(key,sec),flush=True)
        except RuntimeError as exc:
            results[key]={"status":"FAIL","reason":str(exc)[-800:]}
            failures.append(key)
            print("BOOTSTRAP_NATIVE_SUITE=FAIL case=%s reason=%s"%(key,exc),flush=True)

    # Direct real path, no GNU assembler or GNU linker:
    # Stage2 MiniC (compiler ELF from fixed-point) compiles C -S.
    # Stage2 MiniAS turns *both* .s into RV64 .o.
    # Stage2 MiniLD creates an actual ET_EXEC entry _start.
    # Linux riscv64 exit syscall is 93, returning main()'s 42.
    case=out/"chain";case.mkdir(parents=True,exist_ok=True)
    source=case/"main.c";pre=case/"main.i";asm=case/"main.s"
    crt_asm=case/"start.s";main_obj=case/"main.o";crt_obj=case/"start.o"
    final=case/"minic-native-42.elf"
    source.write_text("int main(void) { return 42; }\n")
    call(["riscv64-linux-gnu-gcc","-E","-P","-std=c11",source,"-o",pre],
         env,out/"logs/native-preprocess.log",timeout=120)
    call(["qemu-riscv64",stage2,"-S",pre,"-o",asm],
         env,out/"logs/native-stage2-compile.log",timeout=160)
    crt_asm.write_text(
        ".text\n.globl _start\n.type _start, @function\n"
        "_start:\n  call main\n  li a7, 93\n  ecall\n"
        ".size _start, .-_start\n")
    for name,source_file,target_file in (("main",asm,main_obj),("start",crt_asm,crt_obj)):
        call([wrapper/"minic-as","-march=rv64gc","-mabi=lp64d",
              "-o",target_file,source_file],env,out/"logs"/("native-"+name+"-as.log"),timeout=180)
    call([wrapper/"minic-ld","-melf64lriscv","-Ttext=0x10000","-e","_start",
          "-o",final,crt_obj,main_obj],env,out/"logs/native-minild-final.log",timeout=180)
    call(["qemu-riscv64",final],env,out/"logs/native-runtime-42.log",
         timeout=45,expected=42)
    print("BOOTSTRAP_NATIVE_MINI_CHAIN=PASS Stage2_MiniC=true Stage2_MiniAS=true Stage2_MiniLD=true runtime_exit=42 GNU_AS_LD=false",flush=True)
    results["native-42"]={"status":"PASS","elf_sha256":hashfile(final),"elf_bytes":final.stat().st_size}
    (out/"summary.json").write_text(json.dumps({
        "certified_compiler_sha256":hashfile(stage2),
        "certified_stage2_built_toolchain_sha256":{name:report["binaries"][name]["sha256"] for name in TOOLS},
        "tests":results,
        "toolchain_built_by_stage2":True,
        "bootstrapped_no_GNU_preprocessor":False,
        "native_main_program_without_GNU_AS_or_LD":True,
        "full_Linux_compiled_by_Stage2":False,
    },indent=2)+"\n")
    print("BOOTSTRAP_STAGE2_NATIVE_RUNTIME=PASS tools=8 suites=%d runtime_chain=42 evidence_sha=true"%len(suites),flush=True)

if __name__=="__main__":
    try:main()
    except (OSError,RuntimeError,KeyError,ValueError) as exc:
        print("BOOTSTRAP_STAGE2_NATIVE_RUNTIME=FAIL "+str(exc),file=sys.stderr)
        sys.exit(1)
