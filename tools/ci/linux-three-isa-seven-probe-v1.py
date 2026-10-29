#!/usr/bin/env python3
"""Seven independent MiniC -S replay shards consume SHA-frozen GNU .i.

Discovery only: a successful .s is NOT GNU assembly/link/QEMU proof.
"""
import argparse,concurrent.futures,hashlib,json,os,re,subprocess,sys
# Opt-in complete machine reject kind census, distinct from C diagnostic text.
# The Direct LP64 residual emitters already print these trace records when
# MINIC_M1_TRACE=1; gather ALL emitted kind owners with their source TU.
# General shared Direct LP64 owner rejection includes source stage,
# opcode kind, and backend line even when the residual M8 selector was
# never reached. Archive every machine reject, not just M8 default cases.
ASM_FORM = re.compile(
    r"^MINIC_BACKEND_ASM_FORM arch=(aarch64|loongarch64) "
    r"function=([A-Za-z_][A-Za-z_0-9]*) id=(\d+) "
    r"template_length=(\d+) hash=([0-9a-f]{16}) "
    r"preview=([A-Za-z_0-9]+) clobbers=(\d+) "
    r"memory=([01]) goto=([01])$",re.MULTILINE)
def all_asm_form_frontiers(stderr,target):
    return [dict(function=m.group(2),id=int(m.group(3)),
                 template_length=int(m.group(4)),hash=m.group(5),
                 preview=m.group(6),clobbers=int(m.group(7)),
                 memory=int(m.group(8)),goto=int(m.group(9)))
            for m in ASM_FORM.finditer(stderr) if m.group(1)==target]

PREFLIGHT_ROOT = re.compile(
    r"^MINIC_BACKEND_PREFLIGHT_ROOT arch=(aarch64|loongarch64) "
    r"function=([A-Za-z_][A-Za-z_0-9]*) reason=([a-z_]+) "
    r"blocks=(\d+) parameters=(\d+) inline=(\d+)$",re.MULTILINE)
def all_preflight_roots(stderr,target):
    return [dict(function=m.group(2),reason=m.group(3),
                 blocks=int(m.group(4)),parameters=int(m.group(5)),
                 inline=int(m.group(6)))
            for m in PREFLIGHT_ROOT.finditer(stderr)
            if m.group(1)==target]

BACKEND_REJECT = re.compile(
    r"^MINIC_BACKEND_REJECT arch=(aarch64|loongarch64) "
    r"function=([^ ]+) stage=([^ ]+) block=(-?\d+) "
    r"instruction=(-?\d+) kind=(-?\d+) backend_line=(\d+)$",re.MULTILINE)
def all_backend_reject_frontiers(stderr,target):
    return [dict(function=m.group(2),stage=m.group(3),
                 block=int(m.group(4)),instruction=int(m.group(5)),
                 kind=int(m.group(6)),backend_line=int(m.group(7)))
            for m in BACKEND_REJECT.finditer(stderr) if m.group(1)==target]

# Preserve source and ABI type identities of unsupported AArch64 scalar
# LOADs. This is diagnostic evidence only and never changes admission.
AARCH64_LOAD_SHAPE_REJECT = re.compile(
    r"^MINIC_AARCH64_LOAD_SHAPE_REJECT result_base=(\d+) "
    r"result_rank=(\d+) result_ptr=(\d+) result_enum=(\d+) "
    r"address_id=(\d+) address_base=(-?\d+) address_ptr=(\d+)$",
    re.MULTILINE)

def all_load_shape_frontiers(stderr,target):
    if target != "aarch64":
        return []
    return [dict(result_base=int(m.group(1)),result_rank=int(m.group(2)),
                 result_ptr=int(m.group(3)),result_enum=int(m.group(4)),
                 address_id=int(m.group(5)),address_base=int(m.group(6)),
                 address_ptr=int(m.group(7)))
            for m in AARCH64_LOAD_SHAPE_REJECT.finditer(stderr)]

MACHINE_OPCODE_REJECT = re.compile(
    r"^MINIC_M8_OPCODE_REJECT arch=(aarch64|loongarch64) "
    r"function=([A-Za-z_][A-Za-z_0-9]*) block=(\d+) "
    r"instruction=(\d+) kind=(\d+)$", re.MULTILINE)

def all_machine_opcode_frontiers(stderr, target):
    return [dict(function=m.group(2), block=int(m.group(3)),
                 instruction=int(m.group(4)), kind=int(m.group(5)))
            for m in MACHINE_OPCODE_REJECT.finditer(stderr)
            if m.group(1)==target]

CORE_FUNCTION_FAILURE = re.compile(
    r"^MINIC_CORE_FUNCTION_FAILURE kind=(UNSUPPORTED|ERROR) index=(\d+) "
    r"function=([A-Za-z_][A-Za-z_0-9]*)$", re.MULTILINE)

def all_core_function_frontiers(stderr):
    # The compiler's post-prune validation has already lowered all reachable
    # functions; preserve every result, not only the first error per TU.
    return [dict(kind=m.group(1), index=int(m.group(2)), function=m.group(3))
            for m in CORE_FUNCTION_FAILURE.finditer(stderr)]

# Collect source-to-Core failure context without conflating same-text
# diagnostics with a proven target-neutral machine root cause.
CORE_LOWER_TRACE = re.compile(r"^CORE_FAST_TRACE ([^\r\n]+)$", re.MULTILINE)
CORE_TRACE_FIELD = re.compile(r"([a-z_]+)=([A-Za-z0-9_.:-]+)")

# Common Core emits this for rejected qualified operands or when no machine
# contract admits the source.  Stable signatures retain target-owned syntax
# differences instead of treating all INLINE_ASM (kind=5) as one fixable root.
CORE_ASM_FRONTIER = re.compile(
    r"^CORE_ASM_FRONTIER function=([A-Za-z_][A-Za-z_0-9]*) "
    r"reason=(target_required|target_lower|immediate_output|structured_roles|"
    r"fixed_register|atomic_immediate|mixed_memory|readwrite_output|"
    r"empty_barrier|no_handler) template_length=(\d+) "
    r"template_hash=([0-9a-f]{16}) signature_hash=([0-9a-f]{16}) "
    r"preview=([A-Za-z_0-9]+) outputs=(\d+) inputs=(\d+) "
    r"labels=(\d+) reg_clobbers=(\d+) clobbers=(\d+) "
    r"memory=([01]) goto=([01]) volatile=([01])$", re.MULTILINE)

def all_core_asm_frontiers(stderr):
    seen, records = set(), []
    for m in CORE_ASM_FRONTIER.finditer(stderr):
        fields = m.groups()
        key = fields
        if key in seen:
            continue
        seen.add(key)
        records.append(dict(
            function=fields[0], reason=fields[1], template_length=int(fields[2]),
            template_hash=fields[3], signature_hash=fields[4], preview=fields[5],
            outputs=int(fields[6]), inputs=int(fields[7]), labels=int(fields[8]),
            reg_clobbers=int(fields[9]), clobbers=int(fields[10]),
            memory=int(fields[11]), goto=int(fields[12]),
            volatile=int(fields[13])))
    return records

def all_core_lower_trace_frontiers(stderr):
    seen, records = set(), []
    for match in CORE_LOWER_TRACE.finditer(stderr):
        fields = dict(CORE_TRACE_FIELD.findall(match.group(1)))
        function = fields.get("function", "")
        stage = fields.get("stage", "")
        reason = fields.get("reason", "")
        kind = fields.get("kind", "na")
        status = fields.get("status", "na")
        if (not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", function)
                or not re.fullmatch(r"[a-z_]+", stage)
                or not re.fullmatch(r"[a-z_-]+", reason)
                or not re.fullmatch(r"(?:na|-?[0-9]+)", kind)
                or not re.fullmatch(r"(?:na|-?[0-9]+)", status)):
            continue
        key = (function, stage, reason, kind, status)
        if key not in seen:
            seen.add(key)
            records.append(dict(function=function, stage=stage, reason=reason,
                                kind=kind, status=status))
    return records

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
            env=os.environ.copy()
            env["MINIC_CORE_FAILURE_CENSUS"]="1"
            env["MINIC_M1_TRACE"]="1"
            env["MINIC_BACKEND_CENSUS"]="1"
            env["MINIC_ASM_FRONTIER_TRACE"]="1"
            p=subprocess.run([str(compiler),"--target="+a.target+"-linux",
                              *row["minic_flags"],"-S",str(file),"-o",str(asm)],
                             capture_output=True,timeout=a.timeout,env=env)
            ok=p.returncode==0 and asm.is_file() and asm.stat().st_size>0
            if not ok:asm.unlink(missing_ok=True)
            error_text=p.stderr.decode(errors="replace")
            lines=error_text.splitlines()
            core_frontiers=all_core_function_frontiers(error_text)
            core_lower_traces=all_core_lower_trace_frontiers(error_text)
            asm_frontiers=all_core_asm_frontiers(error_text)
            machine_frontiers=all_machine_opcode_frontiers(error_text,a.target)
            backend_frontiers=all_backend_reject_frontiers(error_text,a.target)
            load_shape_frontiers=all_load_shape_frontiers(error_text,a.target)
            preflight_roots=all_preflight_roots(error_text,a.target)
            asm_forms=all_asm_form_frontiers(error_text,a.target)
            if core_frontiers and ok:
                raise ValueError("MiniC succeeded despite failed Core functions: "+obj)
            if len({f["index"] for f in core_frontiers}) != len(core_frontiers):
                raise ValueError("duplicate Core frontier identity for "+obj)
            diag=next((v for v in lines if "error:" in v.lower()),lines[0] if lines else "")
            diag=re.sub(r"[^ :]+\.(?:i|c|h):\d+(?::\d+)?:","<source>:",diag)[:240]
            return dict(index=row["index"],object=obj,status="MINIC_S_PASS" if ok else "MINIC_FAIL",
                        diagnostic=diag,input_sha256=row["sha256"],
                        core_frontiers=core_frontiers,
                        core_lower_traces=core_lower_traces,
                        asm_frontiers=asm_frontiers,
                        machine_frontiers=machine_frontiers,
                        backend_frontiers=backend_frontiers,
                        load_shape_frontiers=load_shape_frontiers,
                        preflight_roots=preflight_roots,asm_forms=asm_forms,
                        asm_sha256=sha(asm) if ok else None)
        except subprocess.TimeoutExpired:
            asm.unlink(missing_ok=True)
            return dict(index=row["index"],object=obj,status="MINIC_TIMEOUT",diagnostic="timeout",
                        core_frontiers=[],core_lower_traces=[],machine_frontiers=[],
                        backend_frontiers=[],load_shape_frontiers=[],
                        preflight_roots=[],asm_forms=[],asm_frontiers=[],
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
