#!/usr/bin/env python3
"""Symbolize potential RISC-V return addresses. Not a verified stack unwind."""
import argparse
import bisect
import collections
import json
from pathlib import Path
import re
import subprocess

WORD = re.compile(r"0x([0-9a-fA-F]{16})")

def code_symbols(vmlinux):
    proc = subprocess.run(["riscv64-linux-gnu-nm", "-n", "-S",
                           "--defined-only", str(vmlinux)],
                          capture_output=True,text=True,check=True)
    result=[]
    for line in proc.stdout.splitlines():
        cols=line.split(None,3)
        if len(cols)!=4 or cols[2] not in ("T","t","W","w"):
            continue
        try:
            a=int(cols[0],16); size=int(cols[1],16)
        except ValueError:
            continue
        if size: result.append((a,a+size,cols[3]))
    return sorted(result)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--jsonl",type=Path,required=True)
    ap.add_argument("--vmlinux",type=Path,required=True)
    ap.add_argument("--output",type=Path,required=True)
    args=ap.parse_args()
    syms=code_symbols(args.vmlinux)
    starts=[x[0] for x in syms]
    hits=collections.Counter()
    evidence=[]
    n=0
    late_registers=[]
    for ln in args.jsonl.read_text().splitlines():
        x=json.loads(ln);t=x["at_seconds"];n+=1
        regs=x.get("registers",{})
        if t>=12:
            late_registers.append(regs)
        for register in ("pc", "ra", "sepc"):
            raw=regs.get(register)
            if not raw: continue
            try: addr=int(raw,16)
            except ValueError: continue
            j=bisect.bisect_right(starts,addr)-1
            if j>=0 and syms[j][0]<=addr<syms[j][1]:
                base,_,name=syms[j]
                evidence.append(f"REGISTER_CODE t={t} reg={register} symbol={name}+0x{addr-base:x}")
            else:
                evidence.append(f"REGISTER_CODE t={t} reg={register} symbol=UNRESOLVED addr=0x{addr:x}")
        rs=[]
        if x.get("registers",{}).get("ra"):
            rs.append(("ra",int(x["registers"]["ra"],16)))
        rs.extend((f"stack_{i}",int(v,16)) for i,v in enumerate(WORD.findall(x.get("stack_window",""))))
        found=set()
        for source,addr in rs:
            j=bisect.bisect_right(starts,addr)-1
            if j>=0 and syms[j][0]<=addr<syms[j][1]:
                base,_,name=syms[j];found.add(name)
                evidence.append(f"STACK_CODE_CANDIDATE t={t} source={source} symbol={name}+0x{addr-base:x}")
        for name in found:hits[name]+=1
        evidence.append(f"STACK_SAMPLE t={t} plausible_code_functions={len(found)}")
    lines=[f"STACK_ANALYSIS=READ_ONLY_NOT_UNWIND samples={n} unique_functions={len(hits)}"]
    if late_registers:
        def unique(key):
            return len({r.get(key) for r in late_registers if r.get(key)})
        lines.append(f"REGISTER_LATE_DIVERSITY samples={len(late_registers)} "
                     f"unique_pc={unique('pc')} unique_ra={unique('ra')} "
                     f"unique_sepc={unique('sepc')} unique_sp={unique('sp')} "
                     f"unique_a1={unique('a1')}")
    lines.extend(f"STACK_FREQUENCY count={v} symbol={k}" for k,v in hits.most_common(35))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text("\n".join(lines+evidence)+"\n")
    print("\n".join(lines))
if __name__=="__main__":main()
