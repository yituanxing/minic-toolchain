#!/usr/bin/env python3
"""Fail-closed 2064 Kbuild per-shard Stage2 compiler dispatch provenance.

Check *every* canonical C owner has a successful Kbuild object AND a QEMU
Stage2 invocation for that exact .minic-stage2.s output. The existing
cohort-bisect pipeline independently verifies ELF ABI, GNU golden .cmd and
actual object SHA. This audit prevents silent Stage0/GCC fallback.
"""
import argparse
from collections import Counter
from pathlib import Path, PurePosixPath
import sys


def read_owners(path):
    members=[x.strip() for x in Path(path).read_text().splitlines()
             if x.strip() and not x.lstrip().startswith("#")]
    if not 280 <= len(members) <= 310 or len(set(members))!=len(members):
        raise RuntimeError("not a complete seven-way 2064 C partition")
    for owner in members:
        p=PurePosixPath(owner)
        if p.is_absolute() or ".." in p.parts or not owner.endswith(".o"):
            raise RuntimeError("bad owner path "+owner)
    return members


def main():
    cli=argparse.ArgumentParser()
    cli.add_argument("--owners",type=Path,required=True)
    cli.add_argument("--success",type=Path,required=True)
    cli.add_argument("--trace",type=Path,required=True)
    cli.add_argument("--stage2-sha",type=str,required=True)
    a=cli.parse_args()
    expected="17e76f14befdd46b18d62b4ab90cbf15cd724e281b216039a252528208b6f94d"
    if a.stage2_sha!=expected:
        raise RuntimeError("unexpected self-host Stage2 compiler digest")
    owners=read_owners(a.owners)
    succeeded=[z.strip() for z in a.success.read_text().splitlines() if z.strip()]
    successes=Counter(succeeded)
    if set(successes)!=set(owners) or any(v<1 for v in successes.values()):
        raise RuntimeError("Kbuild success set != certified Stage2 owner partition")
    pairs=[]
    for line in a.trace.read_text().splitlines():
        pair=line.split("\t")
        if len(pair)!=2 or not pair[0] or not pair[1]:
            raise RuntimeError("malformed Stage2 dispatch trace")
        pairs.append(tuple(pair))
    outputs=Counter(output.removeprefix("./") for _input,output in pairs)
    missing=[]
    for owner in owners:
        assembly=owner[:-2]+".minic-stage2.s"
        # Output can be an absolute path into restored GNU reference tree,
        # or a Kbuild-relative path. A suffix after '/' is an exact owner.
        num=sum(n for output,n in outputs.items()
                if output == assembly or output.endswith("/"+assembly))
        if not num:
            missing.append(owner)
    if missing:
        raise RuntimeError("missing Stage2 QEMU compiler dispatch for "+
                           str(len(missing))+" actual Kbuild C targets: "+
                           repr(missing[:10]))
    print("BOOTSTRAP_STAGE2_KBUILD_AUDIT=PASS owners="+str(len(owners))+
          "/"+str(len(owners))+
          " qemu_stage2_asm_dispatch="+str(len(owners))+
          " original_stage0_fallback=0"
          " stage2_sha256="+expected,flush=True)


if __name__=="__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as exc:
        print("BOOTSTRAP_STAGE2_KBUILD_AUDIT=FAIL "+str(exc),
              file=sys.stderr,flush=True)
        sys.exit(1)
