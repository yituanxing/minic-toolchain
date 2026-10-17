#!/usr/bin/env python3
"""Diagnostic-only: audit cross-run assembly SHA discrepancies with the EXACT
same SHA-frozen Linux input and exact same current minic executable.

Old/core500 and resource500 generated different assembly SHA in 164/500 cases.
Do NOT infer a compiler regression: first discriminate runtime environment
switches and output pathname from genuine translation instability.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

EXPECTED={
 "init/main.o":("3c59ade4745b3a344717d3f40de7635e8a9c1136747db5b0204711ed7fd18522",
                "2a04eae021d9b9975e978b69757a18e04f5d3bf98ad47dec67c49169633c741a"),
 "kernel/sched/core.o":("8ee36f29c49d86b104e0904e690c5a9184e3ce1bf43ae4dbf312413e52c12132",
                        "f990546c0150ea8e2d876fa804829e121ba0cd96cd269253eda5eedb4e428c62"),
 "kernel/events/core.o":("479680fb3408d784c2a8a76d82f27ebca242a209bdb58393b2a58eaf87f5ec5b",
                           "f862b21cff3a01ad0f67fb325a5e5873d1954c10ef6577bd4c19f55b5e8eb030"),
 "fs/ext4/fsync.o":("ad8360ebd95c22eccdedc0c315f9d8ed32694c5c8bfeeaaa4f8cb27fde8661c1",
                       "ad8360ebd95c22eccdedc0c315f9d8ed32694c5c8bfeeaaa4f8cb27fde8661c1"),
}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    cli=argparse.ArgumentParser()
    for opt in ("corpus","driver","out"):
        cli.add_argument("--"+opt,required=True,type=Path)
    a=cli.parse_args()
    a.out.mkdir(parents=True,exist_ok=True)
    manifest={}
    for row in (a.corpus/"selected-tus.txt").read_text().splitlines():
        _,obj,rel,_=row.split("\t",3)
        manifest[obj]=rel
    assert len(manifest)==500
    base=(a.corpus/"kbuild").resolve()
    rows=[]
    for name,(old_sha,current_sha) in EXPECTED.items():
        frozen=(base/manifest[name]).resolve()
        if not frozen.is_relative_to(base) or not frozen.is_file():
            raise RuntimeError("not a real cached Linux input "+name)
        case={"object":name,"input_sha256":sha(frozen),"old_run_sha256":old_sha,
              "new_run_sha256":current_sha,"modes":{}}
        for mode in ("plain","flags"):
            env=os.environ.copy()
            env.pop("MINIC_LINUX_FULL_CROSSBLOCK",None)
            env.pop("MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ",None)
            if mode=="flags":
                env.update(MINIC_LINUX_FULL_CROSSBLOCK="1",
                           MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ="1")
            variants=[]
            for label in ("old","new"):
                prefix=("minic-pair-" if label=="old" else "minic-current-resource-")
                with tempfile.TemporaryDirectory(prefix=prefix) as td:
                    out=Path(td)/("old.s" if label=="old" else "result.s")
                    proc=subprocess.run([str(a.driver),"-S",str(frozen),"-o",str(out)],
                                        env=env,capture_output=True,text=True,
                                        errors="replace",timeout=120)
                    if proc.returncode or not out.is_file():
                        raise RuntimeError("compile failed "+name+" "+mode+" "+label+" "+proc.stderr[-1000:])
                    variants.append(sha(out))
            case["modes"][mode]={"same_sha_for_distinct_output_name":variants[0]==variants[1],
                                 "sha256":variants[0],
                                 "matches_previous_core500":variants[0]==old_sha,
                                 "matches_current_resource500":variants[0]==current_sha}
            print("MINIC_RESOURCE_REPRO owner=%s mode=%s same_output_hash=%s old=%s new=%s"%
                  (name,mode,case["modes"][mode]["same_sha_for_distinct_output_name"],
                   case["modes"][mode]["matches_previous_core500"],
                   case["modes"][mode]["matches_current_resource500"]),flush=True)
        rows.append(case)
    result={"cases":4,"run_sha256":sha(a.driver),"rows":rows}
    (a.out/"repro-summary.json").write_text(json.dumps(result,indent=2)+"\n")
    if not all(x["same_sha_for_distinct_output_name"] for row in rows
               for x in row["modes"].values()):
        raise RuntimeError("same-source output differs with output file name")
    print("MINIC_RESOURCE_REPRO=PASS four_owners=4 identical_within_mode=true",flush=True)

if __name__=="__main__":main()
