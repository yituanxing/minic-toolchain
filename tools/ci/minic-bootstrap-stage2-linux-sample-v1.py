#!/usr/bin/env python3
"""Stage2 Linux correctness gate: 20 fixed GNU Kbuild TUs, Stage0 vs Stage2.

Reuse the existing SHA-certified first500 Linux .i cache, old replay and
assembler scripts, and immutable bootstrap Stage2. Do NOT re-preprocess Linux
or confuse compiler self-consistency with independent semantic correctness.

Verify: 500 input SHA, exact 10 hot/5 medium/5 light owners from repository,
20/20 Stage0 and Stage2 compile success, byte-identical assembly and real
GNU-AS RV64 .o ELF identities. Stage2 may execute under qemu-riscv64 wrapper.
Linux Image/QEMU P1 is a distinct next gate and is NOT claimed here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SELECTION = ROOT / "tests/compiler/perf/minic-linux-phase20-selected-2026-10-16.tsv"
PERF_CORPUS_SIZE = 500


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as data:
        for chunk in iter(lambda: data.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def command(args, *, env, label, timeout=1000):
    try:
        p = subprocess.run([str(x) for x in args], cwd=ROOT, env=env,
                           text=True, errors="replace", stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, check=False, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise RuntimeError(label + " timeout") from None
    if p.returncode:
        print(p.stdout[-12000:], file=sys.stderr, flush=True)
        raise RuntimeError(label+" status="+str(p.returncode))
    print(p.stdout[-9000:], flush=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--corpus", type=Path, required=True)
    p.add_argument("--stage0", type=Path, required=True)
    p.add_argument("--stage2", type=Path, required=True,
                   help="stage2 qemu-riscv64 wrapper executable")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--jobs", type=int, choices=(1,2,3,4), default=3)
    args = p.parse_args()
    corpus, stage0, stage2, out = (
        args.corpus.resolve(), args.stage0.resolve(),
        args.stage2.resolve(), args.out.resolve())
    out.mkdir(parents=True, exist_ok=True)
    for binary in (stage0,stage2):
        if not binary.is_file():
            raise RuntimeError("missing compiler executable "+str(binary))
    if sha(stage0) != "f04794d3132631059c028e0a889bf8cf06f15e8f24afcc70d24697edce4925fd":
        raise RuntimeError("Stage0 is not the exact current B1 certified compiler")
    master = corpus / "selected-tus.txt"
    manifest = []
    for line in master.read_text().splitlines():
        index, owner, rel_i, source = line.split("\t", 3)
        manifest.append((int(index),owner,rel_i,source))
    if len(manifest) != PERF_CORPUS_SIZE or len(set(x[0] for x in manifest)) != 500 \
       or len(set(x[1] for x in manifest)) != 500:
        raise RuntimeError("noncanonical first500 frozen corpus manifest")
    expected_hashes = {}
    input_root=(corpus/"kbuild").resolve()
    for line in (corpus/"inputs.sha256").read_text().splitlines():
        digest,path = line.split(maxsplit=1)
        # SHA utility records normalized filesystem paths, but genuine Kbuild
        # object names may include relative ../../../virt/kvm/... lexical
        # components. Both forms resolve to the exact same frozen file.
        candidate=Path(path.lstrip("*")).resolve()
        if not candidate.is_relative_to(input_root) or candidate in expected_hashes:
            raise RuntimeError("noncanonical or duplicate frozen input SHA path "+path)
        expected_hashes[candidate] = digest
    if len(expected_hashes)!=500:
        raise RuntimeError("bad corpus SHA manifest count")
    byowner = {x[1]:x for x in manifest}
    members=[]
    seen=set()
    groups={"hot":0,"middle":0,"light":0}
    for row in SELECTION.read_text().splitlines():
        if not row.strip() or row.lstrip().startswith("#"):
            continue
        group,owner,prior = row.split("\t")
        if group not in groups or owner not in byowner or owner in seen:
            raise RuntimeError("invalid stratification "+row)
        groups[group] += 1
        seen.add(owner)
        members.append((group,*byowner[owner]))
    if len(members)!=20 or groups!={"hot":10,"middle":5,"light":5}:
        raise RuntimeError("not full canonical hot/middle/light20 selection")
    # SHA check the selected twenty AND the entire frozen corpus's 500 .i
    # before computing any compiler outputs.
    for i,owner,rel,source in manifest:
        name=(input_root/rel).resolve()
        if not name.is_relative_to(input_root):
            raise RuntimeError("frozen Kbuild .i escaped corpus "+owner)
        expected=expected_hashes.get(name)
        if not expected or not name.is_file() or sha(name)!=expected:
            raise RuntimeError("source .i SHA mismatch "+owner+" "+str(name))
    print("BOOTSTRAP_STAGE2_LINUX_INPUT=PASS corpus=500 sha=500/500 "
          "selection=10hot5middle5light source=linux-6.6.143", flush=True)
    indices=",".join(str(m[1]) for m in members)
    env=dict(os.environ, MINIC_LINUX_FULL_CROSSBLOCK="1",
             MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ="1")
    for name,binary in (("stage0",stage0),("stage2",stage2)):
        work=out/name
        command([sys.executable, "tests/external/linux/corpus_replay.py",
                 "--minic",binary,"--corpus",corpus,"--work",work,
                 "--jobs",args.jobs,"--indices",indices],env=env,
                label=name+"_LINUX_REPLAY",timeout=1700)
        results=json.loads((work/"batch-results.json").read_text())
        if (len(results)!=20 or any(x["status"]!="PASS" for x in results) or
                {int(x["index"]) for x in results}!={m[1] for m in members}):
            raise RuntimeError(name+" 20/20 C compilation was not PASS")
        command([sys.executable, "tests/external/linux/assemble_replay.py",
                 "--work",work,"--jobs",args.jobs],env=env,
                label=name+"_LINUX_ASSEMBLE",timeout=500)
        obj=json.loads((work/"assemble-results.json").read_text())
        if len(obj)!=20 or any(x["status"]!="PASS" for x in obj):
            raise RuntimeError(name+" 20/20 real RV64 object assembling did not PASS")
        print("BOOTSTRAP_STAGE2_LINUX_"+name.upper()+"=PASS"
              " objects=20 assembled=20",flush=True)
    records=[]
    for group,index,owner,rel,source in members:
        first=out/"stage0"/"minic-out"/(rel+".s")
        second=out/"stage2"/"minic-out"/(rel+".s")
        # the existing assembler replay naming is numbered four digit index
        first_o=out/"stage0"/"object-out"/f"{index:04d}.o"
        second_o=out/"stage2"/"object-out"/f"{index:04d}.o"
        matching_asm=first.is_file() and second.is_file() and sha(first)==sha(second)
        matching_obj=first_o.is_file() and second_o.is_file() and sha(first_o)==sha(second_o)
        row={"group":group,"index":index,"object":owner,"input":rel,
             "input_sha256":sha(corpus/"kbuild"/rel),
             "assembly_same":matching_asm,
             "object_same":matching_obj,
             "stage0_assembly_sha":sha(first) if first.is_file() else None,
             "stage2_assembly_sha":sha(second) if second.is_file() else None,
             "stage0_object_sha":sha(first_o) if first_o.is_file() else None,
             "stage2_object_sha":sha(second_o) if second_o.is_file() else None}
        records.append(row)
        print(f"BOOTSTRAP_STAGE2_LINUX_TU owner={owner} group={group}"
              f" asm={'MATCH' if matching_asm else 'DIFF'}"
              f" object={'MATCH' if matching_obj else 'DIFF'}",flush=True)
    report={"schema":"minic-stage2-linux-20-exact-v1",
            "corpus_size":500, "selected":len(members), "groups":groups,
            "input_sha_verified":500,
            "stage0_sha":sha(stage0),
            "assembly_equal":sum(x["assembly_same"] for x in records),
            "object_equal":sum(x["object_same"] for x in records),
            "stage2_scope":"qemu-riscv64-rv64-self-host-compiler",
            "link_qemu_p1":"NOT_RUN",
            "rows":records}
    (out/"verdict.json").write_text(json.dumps(report,indent=2)+"\n")
    if report["assembly_equal"]!=20 or report["object_equal"]!=20:
        raise RuntimeError("Stage2 differs from Stage0 on Linux .s/.o")
    print("BOOTSTRAP_STAGE2_LINUX20=PASS owners=20/20"
          " stage0_stage2_asm=20/20"
          " rv64_objects=20/20"
          " complete_linux_image=NOT_RUN qemu_kernel_p1=NOT_RUN",flush=True)


if __name__=="__main__":
    try:main()
    except (OSError,ValueError,RuntimeError) as e:
        print("BOOTSTRAP_STAGE2_LINUX20=FAIL "+str(e),file=sys.stderr,flush=True)
        sys.exit(1)
