#!/usr/bin/env python3
"""Aggregate seven existing CI shard artifacts: exactly 500 Linux TU identities.

Success requires 7 shards, 500 distinct source-index/object pairs, canonical
non-overlap by *manifest position*, 500/500 Stage0 == genuine QEMU Stage2
.s SHA AND real GNU-AS .o SHA, and each shard's input manifest 500/500.
This checks reproducibility, not independent kernel P1 runtime correctness.
"""
import json
from pathlib import Path
import sys

EXPECTED = {"a": (0,72), "b": (72,72), "c": (144,72),
            "d": (216,71), "e": (287,71), "f": (358,71), "g": (429,71)}


def main():
    folder=Path(sys.argv[1])
    output=Path(sys.argv[2])
    candidate="stage2"
    if len(sys.argv)==4 and sys.argv[3]=="--stage4":
        candidate="stage4"
    elif len(sys.argv)!=3:
        raise RuntimeError("usage: aggregate SHARD_ROOT OUTPUT [--stage4]")
    docs=list(folder.rglob("verdict.json"))
    if len(docs)!=7:
        raise RuntimeError("seven shard verdicts required; found "+str(len(docs)))
    rows=[]
    shard_summary=[]
    for label,(off,n) in EXPECTED.items():
        matches=[p for p in docs if
                 ("minic-bootstrap-"+candidate+"-linux500-"+label) in p.parts]
        if len(matches)!=1:
            raise RuntimeError("missing/duplicate shard "+label+" count="+str(len(matches)))
        d=json.loads(matches[0].read_text())
        expected={
            "scope":"manifest_range", "corpus_size":500, "selected":n,
            "range_offset":off, "range_count":n,
            "input_sha_verified":500, "assembly_equal":n, "object_equal":n,
            "stage0_sha":"f04794d3132631059c028e0a889bf8cf06f15e8f24afcc70d24697edce4925fd"}
        for key,value in expected.items():
            if d.get(key)!=value:
                raise RuntimeError("invalid shard "+label+" field "+key+
                                   " expected "+str(value)+" found "+str(d.get(key)))
        if len(d["rows"])!=n or d["groups"]!={"range":n}:
            raise RuntimeError("invalid shard row/group length "+label)
        # Stage2 legacy JSON uses stage2_*_sha; Stage4 frozen replay uses
        # candidate_*_sha to make it impossible to mistake the new guest
        # compiler for the original Stage2 ELF.
        sha_key="stage2" if candidate=="stage2" else "candidate"
        if any(not x["assembly_same"] or not x["object_same"] or
               x["stage0_assembly_sha"] != x[sha_key+"_assembly_sha"] or
               x["stage0_object_sha"] != x[sha_key+"_object_sha"]
               for x in d["rows"]):
            raise RuntimeError("Stage0/"+candidate+" object or assembly mismatch "+label)
        if candidate=="stage4" and (d.get("candidate_label")!="stage4" or
                                   d.get("candidate_scope")!="qemu-riscv64-rv64-self-host-compiler"):
            raise RuntimeError("wrong candidate compiler identity "+label)
        if len({x["index"] for x in d["rows"]})!=n or len({x["object"] for x in d["rows"]})!=n:
            raise RuntimeError("duplicate index or object inside "+label)
        rows.extend(d["rows"])
        shard_summary.append({"label":label,"offset":off,"count":n,
                              "index_min":min(x["index"] for x in d["rows"]),
                              "index_max":max(x["index"] for x in d["rows"])})
        print("BOOTSTRAP_"+candidate.upper()+"_LINUX500_SHARD=PASS label="+label+
              " count="+str(n), flush=True)
    if len(rows)!=500 or len({x["index"] for x in rows})!=500 \
       or len({x["object"] for x in rows})!=500 or \
       len({x["input"] for x in rows})!=500:
        raise RuntimeError("shards do not cover 500 UNIQUE object/index/.i")
    verdict={"status":"PASS", "candidate_stage":candidate,
             "corpus":"frozen first500 Linux 6.6.143",
             "count":500,"asm_equal":500,"obj_equal":500,"shards":shard_summary,
             "qemu_kernel_p1":"NOT_RUN","linux_image_link":"NOT_RUN",
             "candidate_compiler_sha256":
              ("50b3f7763bde2505e17bb545513cb00664286d4e762b3395aea3f29ce724ef31"
               if candidate=="stage4" else
               "17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d"),
             "rows":sorted(rows,key=lambda r:r["index"])}
    output.write_text(json.dumps(verdict, indent=2)+"\n")
    print("BOOTSTRAP_"+candidate.upper()+"_LINUX500=PASS owners=500/500"
          " stage0_"+candidate+"_asm=500/500 rv64_objects=500/500"
          " complete_linux_image=NOT_RUN qemu_kernel_p1=NOT_RUN",flush=True)


if __name__=="__main__":
    try:
        main()
    except (OSError,ValueError,KeyError,RuntimeError,IndexError) as err:
        print("BOOTSTRAP_SELFHOST_LINUX500=FAIL "+str(err),file=sys.stderr)
        sys.exit(1)
