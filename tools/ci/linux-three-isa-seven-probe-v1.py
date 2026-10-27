#!/usr/bin/env python3
"""Seven independent MiniC -S replay shards consume SHA-frozen GNU .i.

Discovery only: a successful .s is NOT GNU assembly/link/QEMU proof.
"""
import argparse,concurrent.futures,hashlib,json,re,subprocess,sys
from pathlib import Path
def sha(p):
    h=hashlib.sha256()
    with p.open("rb") as f:
        for b in iter(lambda:f.read(1048576),b""):h.update(b)
    return h.hexdigest()
def probe(a):
    frozen=json.loads((a.frozen/"manifest.json").read_text())
    if frozen["schema"]!="three-isa-gnu-i-v1" or frozen["identity"]["target"]!=a.target or frozen["errors"]:
        raise ValueError("wrong target or incomplete GNU frozen .i baseline")
    selected=[r for r in frozen["rows"] if r["shard"]==a.shard]
    if not selected or a.shard not in range(7):raise ValueError("empty or invalid shard")
    a.output.mkdir(parents=True,exist_ok=True)
    compiler=a.minic.resolve(strict=True)
    frozen_manifest_sha256=sha(a.frozen/"manifest.json")
    if any(not isinstance(row.get("index"),int)
           or not isinstance(row.get("minic_flags"),list)
           for row in frozen["rows"]):
        raise ValueError("missing original Kbuild ABI flags or index")
    def process(row):
        obj=row["object"]
        from pathlib import PurePosixPath
        p=PurePosixPath(row["input"])
        if p.is_absolute() or ".." in p.parts or p.parts[0]!="i":
            raise ValueError("unsafe frozen input path")
        file=a.frozen/p
        if file.is_symlink() or sha(file)!=row["sha256"]:
            raise ValueError("cached GNU .i SHA mismatch "+obj)
        asm=a.output/PurePosixPath(obj).with_suffix(".s")
        asm.parent.mkdir(parents=True,exist_ok=True)
        try:
            allowed={"-fshort-wchar","-fno-short-wchar",
                     "-fsigned-char","-funsigned-char",
                     "-fno-signed-char","-fno-unsigned-char",
                     "-ffunction-sections","-fno-function-sections",
                     "-fPIE","-fpie","-fPIC","-fpic",
                     "-fno-pie","-fno-PIE","-fno-pic","-fno-PIC"}
            if any(x not in allowed for x in row["minic_flags"]):
                raise ValueError("invalid MiniC replay flags for "+obj)
            p=subprocess.run([str(compiler),"--target="+a.target+"-linux",
                              *row["minic_flags"],"-S",str(file),"-o",str(asm)],
                             capture_output=True,timeout=a.timeout)
            ok=p.returncode==0 and asm.is_file() and asm.stat().st_size>0
            if not ok:asm.unlink(missing_ok=True)
            lines=p.stderr.decode(errors="replace").splitlines()
            diag=next((v for v in lines if "error:" in v.lower()),lines[0] if lines else "")
            diag=re.sub(r"[^ :]+\.(?:i|c|h):\d+(?::\d+)?:","<source>:",diag)[:240]
            return dict(index=row["index"],object=obj,status="MINIC_S_PASS" if ok else "MINIC_FAIL",
                        diagnostic=diag,input_sha256=row["sha256"],
                        asm_sha256=sha(asm) if ok else None)
        except subprocess.TimeoutExpired:
            asm.unlink(missing_ok=True)
            return dict(index=row["index"],object=obj,status="MINIC_TIMEOUT",diagnostic="timeout",
                        input_sha256=row["sha256"])
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as executor:
        data=list(executor.map(process,selected))
    buckets={}
    for row in data:
        key=row["status"]+" | "+row["diagnostic"]
        buckets.setdefault(key,[]).append(row["object"])
    report=dict(schema="minic-linux-three-isa-shard-v1",
                verdict="MINIC_S_DISCOVERY_ONLY_AS_LINK_QEMU_NOT_TESTED",
                target=a.target,shard=a.shard,total_shards=7,
                frozen_identity=frozen["identity"],
                frozen_manifest_sha256=frozen_manifest_sha256,
                frozen_total=len(frozen["rows"]),
                compiler_sha256=sha(compiler),
                rows=data,diagnostic_signatures=buckets)
    (a.output/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    n=sum(x["status"]=="MINIC_S_PASS" for x in data)
    print(f"MINIC_SEVEN_DISCOVERY target={a.target} shard={a.shard}/7 compiled={n}/{len(data)} errors={len(data)-n} groups={len(buckets)}")
    for k,v in sorted(buckets.items(),key=lambda item:-len(item[1]))[:20]:
        print(f"MINIC_ERROR_CLASS count={len(v)} first={v[0]} kind={k[:150]}")
def main():
    p=argparse.ArgumentParser()
    p.add_argument("--frozen",type=Path,required=True)
    p.add_argument("--minic",type=Path,required=True)
    p.add_argument("--target",choices=["x86_64","aarch64","loongarch64"],required=True)
    p.add_argument("--shard",type=int,required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--workers",type=int,default=2)
    p.add_argument("--timeout",type=int,default=100)
    a=p.parse_args()
    try:
        probe(a)
        return 0
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as exc:
        print("MINIC_SEVEN=FAIL "+str(exc),file=sys.stderr)
        return 2
if __name__=="__main__":sys.exit(main())
