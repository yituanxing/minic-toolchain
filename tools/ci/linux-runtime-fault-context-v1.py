#!/usr/bin/env python3
import argparse
import bisect
import json
import re
import subprocess
from pathlib import Path

INSN_RE = re.compile(r"^0x([0-9a-fA-F]+):\s+([0-9a-fA-F]+)\s+(.*)$")
FAULT_RE = re.compile(r"riscv_cpu_do_interrupt:.*epc:0x([0-9a-fA-F]+).*tval:0x([0-9a-fA-F]+).*desc=(\S+)")


def load_symbols(path: Path):
    symbols=[]
    by_name={}
    for line in path.read_text(errors='replace').splitlines():
        p=line.split()
        if len(p)<3:
            continue
        try:
            a=int(p[0],16)
        except ValueError:
            continue
        n=p[2]
        symbols.append((a,n))
        by_name.setdefault(n,a)
    symbols.sort()
    return symbols,[a for a,_ in symbols],by_name


def resolve(symbols,addrs,address):
    i=bisect.bisect_right(addrs,address)-1
    if i<0:
        return ('?',0,None)
    base,name=symbols[i]
    return (name,address-base,base)


def linked_addr(addr, phys_entry, delta):
    if delta is None:
        return addr
    linked_floor=phys_entry+delta
    if addr>=phys_entry and addr<linked_floor:
        return addr+delta
    return addr


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--nm',type=Path,required=True)
    ap.add_argument('--trace',type=Path,required=True)
    ap.add_argument('--frontier-json',type=Path,required=True)
    ap.add_argument('--vmlinux',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--phys-entry',default='0x80200000')
    args=ap.parse_args()

    result=json.loads(args.frontier_json.read_text())
    detail=result.get('fault_detail') or {}
    if not detail:
        raise SystemExit('FAULT_CONTEXT=FAIL no fault_detail')
    fault_epc=int(detail['epc'])
    fault_tval=int(detail['tval'])
    fault_desc=detail['desc']
    phys_entry=int(args.phys_entry,0)

    symbols,addrs,by_name=load_symbols(args.nm)
    anchor=by_name.get('_start') or by_name.get('_text') or by_name.get('_stext')
    delta=(anchor-phys_entry) if anchor is not None and anchor>phys_entry else None

    lines=args.trace.read_text(errors='replace').splitlines()
    fault_index=None
    for i,line in enumerate(lines):
        m=FAULT_RE.search(line)
        if not m:
            continue
        if int(m.group(1),16)==fault_epc and int(m.group(2),16)==fault_tval and m.group(3)==fault_desc:
            fault_index=i
            break
    if fault_index is None:
        raise SystemExit('FAULT_CONTEXT=FAIL fault not found in trace')

    insns=[]
    for i,line in enumerate(lines[:fault_index]):
        m=INSN_RE.match(line.strip())
        if not m:
            continue
        runtime=int(m.group(1),16)
        linked=linked_addr(runtime,phys_entry,delta)
        name,off,base=resolve(symbols,addrs,linked)
        insns.append({
            'line':i,
            'runtime':runtime,
            'linked':linked,
            'symbol':name,
            'offset':off,
            'base':base,
            'encoding':m.group(2),
            'asm':m.group(3).strip(),
        })

    if not insns:
        raise SystemExit('FAULT_CONTEXT=FAIL no instruction records before fault')

    # Collapse executed instructions into consecutive symbol blocks.  This gives
    # the dynamic caller chain immediately preceding the fault, not a static call graph.
    blocks=[]
    for rec in insns:
        key=(rec['symbol'],rec['base'])
        if blocks and blocks[-1]['key']==key:
            blocks[-1]['last']=rec
            blocks[-1]['count']+=1
        else:
            blocks.append({'key':key,'first':rec,'last':rec,'count':1})

    tval_linked=linked_addr(fault_tval,phys_entry,delta)
    tname,toff,tbase=resolve(symbols,addrs,tval_linked)
    epc_linked=linked_addr(fault_epc,phys_entry,delta)
    ename,eoff,ebase=resolve(symbols,addrs,epc_linked)

    last_blocks=[]
    for block in blocks[-24:]:
        r=block['last']
        last_blocks.append({
            'symbol':r['symbol'],
            'last_offset':r['offset'],
            'runtime':r['runtime'],
            'linked':r['linked'],
            'instructions':block['count'],
        })

    recent=insns[-80:]
    calls=[r for r in recent if re.search(r'\b(jal|jalr|call|tail)\b',r['asm'])]

    out={
        'schema':1,
        'fault':{
            'desc':fault_desc,
            'runtime_epc':fault_epc,
            'linked_epc':epc_linked,
            'symbol':ename,
            'offset':eoff,
            'tval':fault_tval,
            'linked_tval':tval_linked,
            'tval_nearest_symbol':tname,
            'tval_symbol_offset':toff,
        },
        'relocation_delta':delta,
        'last_symbol_blocks':last_blocks,
        'recent_calls':calls[-12:],
        'recent_instructions':recent,
    }
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(out,indent=2,sort_keys=True)+'\n')

    print(f'FAULT_CONTEXT=PASS fault={fault_desc}:{ename}+0x{eoff:x}')
    print(f'FAULT_TVAL=0x{fault_tval:016x} linked=0x{tval_linked:016x} nearest={tname}+0x{toff:x}')
    print('DYNAMIC_SYMBOL_TAIL_BEGIN')
    for b in last_blocks:
        print(f"  {b['symbol']}+0x{b['last_offset']:x} runtime=0x{b['runtime']:x} insns={b['instructions']}")
    print('DYNAMIC_SYMBOL_TAIL_END')
    print('RECENT_CALLS_BEGIN')
    for r in calls[-12:]:
        print(f"  {r['symbol']}+0x{r['offset']:x}: {r['asm']}")
    print('RECENT_CALLS_END')

    # Static disassembly around strlen and the last non-strlen dynamic block.
    focus=[]
    if ebase is not None:
        focus.append((ename,ebase,max(64,eoff+32)))
    for b in reversed(last_blocks):
        if b['symbol'] not in ('strlen','?'):
            base=by_name.get(b['symbol'])
            if base is not None:
                focus.append((b['symbol'],base,192))
                break
    for name,base,span in focus:
        print(f'DISASM_BEGIN symbol={name}')
        cmd=['riscv64-linux-gnu-objdump','-d','--start-address',hex(base),'--stop-address',hex(base+span),str(args.vmlinux)]
        proc=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        print(proc.stdout.rstrip())
        print(f'DISASM_END symbol={name}')

    return 0


if __name__=='__main__':
    raise SystemExit(main())
