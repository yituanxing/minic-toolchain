#!/usr/bin/env python3
"""Compiler-only parity contracts for a packed CALL argument ID experiment.

Generate C-only preprocessed (.i) synthetic call boundaries; baseline and
candidate must produce precisely the same .s, or the same rejection when the
call has >64 arguments. No QEMU/GNU assembly is claimed here.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

def parameters(n):
    return ", ".join("long a%d" % i for i in range(n)) or "void"

def values(n):
    return ", ".join(str(i + 1) + "L" for i in range(n))

def make_cases():
    # Inputs are already .i: do not add C comments before MiniC lexical scan.
    lines=[]
    for n in (0,1,2,8,9,63,64):
        lines.append("long f%d(%s);"%(n,parameters(n)))
    lines.append("long variadic(long first, ...);")
    lines.append("long calls(void) {")
    lines.append("    long result = 0;")
    for n in (0,1,2,8,9,63,64):
        lines.append("    result += f%d(%s);"%(n,values(n)))
    lines.append("    result += variadic("+values(64)+");")
    lines.append("    return result;")
    lines.append("}")
    lines.extend(("void cleanup_fn(long *p);",
                  "long cleanup_user(void) {",
                  "    long n __attribute__((cleanup(cleanup_fn))) = 1;",
                  "    return n;",
                  "}"))
    return {"edge-pass":"\n".join(lines)+"\n",
            "too-many-arguments":"long f64("+parameters(64)+");\n"+
              "long bad(void) { return f64("+values(65)+"); }\n"}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(exe, source, output):
    proc=subprocess.run([str(exe),"-S",str(source),"-o",str(output)],
                        capture_output=True,text=True,timeout=180)
    return dict(returncode=proc.returncode,stderr=proc.stderr[-3000:],
                output_sha256=digest(output) if output.is_file() else None)

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--baseline-driver",type=Path,required=True)
    p.add_argument("--candidate-driver",type=Path,required=True)
    p.add_argument("--out",type=Path,required=True)
    a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True)
    report=[]
    for case,text in make_cases().items():
        source=a.out/(case+".i")
        source.write_text(text)
        old=run(a.baseline_driver,source,a.out/(case+".old.s"))
        new=run(a.candidate_driver,source,a.out/(case+".new.s"))
        if case=="edge-pass":
            if old["returncode"] or new["returncode"] or (
                    old["output_sha256"]!=new["output_sha256"]):
                raise SystemExit("MINIC_PACKED_EDGE=FAIL %s old=%s new=%s"%
                                 (case,old,new))
        else:
            if old["returncode"]==0 or new["returncode"]==0:
                raise SystemExit("MINIC_PACKED_EDGE=FAIL overflow accepted "+
                                 json.dumps({"old":old,"new":new}))
        report.append(dict(case=case,input_sha256=digest(source),
                           baseline=old,candidate=new))
        print("MINIC_PACKED_EDGE_CASE=PASS case=%s baseline_rc=%d candidate_rc=%d asm_equal=%s"%
              (case,old["returncode"],new["returncode"],
               str(old["output_sha256"]==new["output_sha256"]).lower()))
    (a.out/"edge-report.json").write_text(json.dumps(report,indent=2)+"\n")
    print("MINIC_PACKED_EDGE=PASS cases=%d old_new_boundaries_identical=true"%len(report))

if __name__=="__main__":
    main()
