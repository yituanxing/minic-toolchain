#!/usr/bin/env python3
"""Report real Boolean range pass scan work, with bit-identical output guards.

This reads the existing exact first500 frozen Linux .i corpus and a throwaway
diagnostic-only current MiniC build. No optimization is applied, and counters
are not normal-speed performance evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

TARGETS=("kernel/bpf/btf.o", "fs/ext4/fsync.o", "arch/riscv/kernel/reset.o")
MARKER=re.compile(r"MINIC_BOOLEAN_HOT_LOOP_COUNTERS_V1\s+([^\n\r]+)")


def sha(path: Path) -> str:
    result=hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1048576), b""):
            result.update(chunk)
    return result.hexdigest()


def compile_one(compiler: Path, source: Path, asm: Path, err: Path) -> str:
    proc=subprocess.run([str(compiler), "-S", str(source), "-o", str(asm)],
                        capture_output=True, text=True, errors="replace",
                        timeout=150, check=False)
    err.write_text(proc.stderr)
    if proc.returncode or not asm.is_file() or asm.stat().st_size==0:
        raise RuntimeError("MiniC failed rc="+str(proc.returncode)
                           + " stderr="+proc.stderr[-900:])
    return sha(asm)


def main() -> None:
    a=argparse.ArgumentParser()
    a.add_argument("--corpus",type=Path,required=True)
    a.add_argument("--reference",type=Path,required=True)
    a.add_argument("--instrumented",type=Path,required=True)
    a.add_argument("--out",type=Path,required=True)
    args=a.parse_args()
    manifest=(args.corpus/"selected-tus.txt").read_text().splitlines()
    if len(manifest)!=500:
        raise RuntimeError("not exact frozen Linux 500")
    pairs={}
    base=(args.corpus/"kbuild").resolve()
    for row in manifest:
        fields=row.split("\t",3)
        if len(fields)!=4:
            raise RuntimeError("invalid frozen TU row")
        _,owner,rel_i,_=fields
        if owner in pairs:
            raise RuntimeError("duplicate frozen owner")
        pairs[owner]=rel_i
    args.out.mkdir(parents=True,exist_ok=True)
    rows=[]
    for index,owner in enumerate(TARGETS):
        if owner not in pairs:
            raise RuntimeError("missing frozen owner "+owner)
        source=(base/pairs[owner]).resolve()
        if not source.is_relative_to(base) or not source.is_file():
            raise RuntimeError("missing authentic preprocessed input")
        target=args.out/str(index)
        target.mkdir()
        reference=compile_one(args.reference,source,target/"reference.s",target/"reference.stderr")
        test=compile_one(args.instrumented,source,target/"instrumented.s",target/"instrumented.stderr")
        if reference!=test:
            raise RuntimeError("diagnostic counter instrument changed .s owner="+owner)
        err=(target/"instrumented.stderr").read_text()
        m=MARKER.findall(err)
        if len(m)!=1:
            raise RuntimeError("expected exactly one Boolean counter line: "+owner)
        fields={}
        for pair in m[0].split():
            name,num=pair.split("=",1)
            fields[name]=int(num)
        expected={"expressions","functions","range_callers",
                  "transitive_expression_visits","transitive_call_nodes",
                  "transitive_source_eligible","transitive_argument_probes",
                  "transitive_fact_positive","direct_variant_probes",
                  "transitive_variant_probes","direct_clones","transitive_clones"}
        if set(fields)!=expected:
            raise RuntimeError("Boolean counter schema mismatch")
        if fields["transitive_expression_visits"]!=fields["range_callers"]*fields["expressions"]:
            raise RuntimeError("Boolean outer scan accounting violated")
        if not (fields["transitive_fact_positive"]<=fields["transitive_source_eligible"]
                <=fields["transitive_call_nodes"]<=fields["transitive_expression_visits"]):
            raise RuntimeError("Boolean useful-work monotonicity violated")
        row={"owner":owner,"input_sha256":sha(source),"assembly_sha256":reference,
             "counters":fields,
             "useful_call_percentage":round(100*fields["transitive_call_nodes"]/max(1,fields["transitive_expression_visits"]),4),
             "fact_positive_percentage":round(100*fields["transitive_fact_positive"]/max(1,fields["transitive_expression_visits"]),6)}
        rows.append(row)
        print("MINIC_BOOLEAN_DIAG_OWNER=PASS owner=%s expr_visits=%d calls=%d fact_positive=%d variant_probes=%d assembly_sha=%s"%
              (owner,fields["transitive_expression_visits"],fields["transitive_call_nodes"],
               fields["transitive_fact_positive"],fields["transitive_variant_probes"],reference),flush=True)
    (args.out/"boolean-scan-diagnostic.json").write_text(
        json.dumps({"schema":"minic-boolean-scan-diagnostic-v1",
                    "source":"real SHA-frozen Linux 6.6.143 C owners",
                    "diagnostic_only":True,
                    "assembly_identical":True,
                    "owner_count":len(rows),"owners":rows},indent=2)+"\n")
    print("MINIC_BOOLEAN_DIAG=PASS owners=3 byte_identical=3/3 "
          "no_production_optimization=true no_speedup_claim=true",flush=True)


if __name__=="__main__":
    main()
